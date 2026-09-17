// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/gates/serialized_access.hpp>
#include <beman/gates/shared_serial_gate.hpp>
#include <beman/execution/execution.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <concepts>
#include <exception>
#include <memory>
#include <semaphore>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace ex = beman::execution;
using namespace beman::gates;

namespace {

struct move_only {
    explicit move_only(int value) : value(value) {}

    move_only(const move_only&)            = delete;
    move_only& operator=(const move_only&) = delete;
    move_only(move_only&&)                 = default;
    move_only& operator=(move_only&&)      = default;

    int value;
};

template <typename T>
concept can_apply_on_rvalue = requires(T&& value) { std::move(value).apply([](auto&) {}); };

template <typename T>
concept can_apply_shared = requires(T& value) { value.apply_shared([](const auto&) {}); };

template <typename T>
concept can_apply_shared_async =
    requires(T& value) { value.apply_shared_async([](const auto&) { return ex::just(); }); };

template <typename T>
concept can_try_apply_shared = requires(T& value) { value.try_apply_shared([](const auto&) {}); };

template <typename T>
concept can_try_apply_shared_async =
    requires(T& value) { value.try_apply_shared_async([](const auto&) { return ex::just(); }); };

struct const_reader {
    void operator()(const int&) const {}
    void operator()(int&) const = delete;
};

/// Extracts the value stored in `sut`.
template <typename T>
auto extract_value(const serialized_access<T>& sut) {
    const auto [r] = *ex::sync_wait(ex::just() | sut.apply([](const T& value) -> T { return value; }));
    return r;
}

/// Extracts the value stored in `sut`, returning the underlying `int` value from the `move_only` object.
int extract_value(serialized_access<move_only>& sut) {
    const auto [r] = *ex::sync_wait(ex::just() | sut.apply([](move_only& value) -> int { return value.value; }));
    return r;
}

} // namespace

TEST(SerializedAccessTest, IsNonMovableAndConstructsValueInPlace) {

    static_assert(!std::copy_constructible<serialized_access<int>>);
    static_assert(!std::move_constructible<serialized_access<int>>);
    static_assert(std::constructible_from<serialized_access<move_only>, std::in_place_t, int>);
    static_assert(!can_apply_on_rvalue<serialized_access<int>>);

    serialized_access<move_only> sut{std::in_place, 42};

    // Act
    ex::sync_wait(ex::just(8) | sut.apply([](move_only& value, int increment) { value.value += increment; }));

    // Assert
    EXPECT_EQ(extract_value(sut), 50);
}

TEST(SerializedAccessTest, SharedFunctionsRequireSharedGateSupport) {
    // Arrange
    using default_access = serialized_access<int>;
    using shared_access  = serialized_access<int, shared_serial_gate>;

    // Act and Assert
    static_assert(!can_apply_shared<default_access>);
    static_assert(!can_apply_shared_async<default_access>);
    static_assert(!can_try_apply_shared<default_access>);
    static_assert(!can_try_apply_shared_async<default_access>);
    static_assert(can_apply_shared<shared_access>);
    static_assert(can_apply_shared_async<shared_access>);
    static_assert(can_try_apply_shared<shared_access>);
    static_assert(can_try_apply_shared_async<shared_access>);
}

TEST(SerializedAccessTest, ApplyProjectsValueBeforePredecessorValues) {

    // Arrange
    serialized_access<int> sut{std::in_place, 10};
    int                    observed_value{0};
    int                    observed_argument{0};

    // Act
    ex::sync_wait(ex::just(32) | sut.apply([&](int& value, int argument) {
        observed_value    = value;
        observed_argument = argument;
        value += argument;
    }));

    // Assert
    EXPECT_EQ(observed_value, 10);
    EXPECT_EQ(observed_argument, 32);
    EXPECT_EQ(extract_value(sut), 42);
}

TEST(SerializedAccessTest, SupportsSharedSerialGateForExclusiveAccess) {
    // Arrange
    serialized_access<int, shared_serial_gate> sut{std::in_place, 10};

    // Act
    ex::sync_wait(ex::just(32) | sut.apply([](int& value, int argument) { value += argument; }));

    const auto [value] = *ex::sync_wait(ex::just() | sut.apply([](const int& current) { return current; }));

    // Assert
    EXPECT_EQ(value, 42);
}

TEST(SerializedAccessTest, SharedApplyPassesConstValueAndPredecessorValues) {
    // Arrange
    serialized_access<int, shared_serial_gate> sut{std::in_place, 10};
    int                                        observed{0};

    // Act
    ex::sync_wait(ex::just(32) |
                  sut.apply_shared([&](const int& value, int argument) { observed = value + argument; }));
    ex::sync_wait(ex::just() | sut.apply_shared(const_reader{}));

    // Assert
    EXPECT_EQ(observed, 42);
}

TEST(SerializedAccessTest, ConstApplyProjectsConstValue) {

    // Arrange
    const serialized_access<int> sut{std::in_place, 7};
    int                          observed{0};

    // Act
    ex::sync_wait(ex::just(5) | sut.apply([&](const int& value, int argument) { observed = value + argument; }));

    // Assert
    EXPECT_EQ(observed, 12);
}

TEST(SerializedAccessTest, AccessFunctionsReturnSenderAdaptorClosures) {

    // Arrange
    serialized_access<int>                     sut{std::in_place, 0};
    serialized_access<int, shared_serial_gate> shared_sut{std::in_place, 0};

    using apply_closure              = decltype(sut.apply([](int&) {}));
    using apply_async_closure        = decltype(sut.apply_async([](int&) { return ex::just(); }));
    using try_apply_closure          = decltype(sut.try_apply([](int&) {}));
    using try_apply_async_closure    = decltype(sut.try_apply_async([](int&) { return ex::just(); }));
    using apply_shared_closure       = decltype(shared_sut.apply_shared([](const int&) {}));
    using apply_shared_async_closure = decltype(shared_sut.apply_shared_async([](const int&) { return ex::just(); }));
    using try_apply_shared_closure   = decltype(shared_sut.try_apply_shared([](const int&) {}));
    using try_apply_shared_async_closure =
        decltype(shared_sut.try_apply_shared_async([](const int&) { return ex::just(); }));

    // Act and Assert
    static_assert(std::derived_from<apply_closure, ex::sender_adaptor_closure<apply_closure>>);
    static_assert(std::derived_from<apply_async_closure, ex::sender_adaptor_closure<apply_async_closure>>);
    static_assert(std::derived_from<try_apply_closure, ex::sender_adaptor_closure<try_apply_closure>>);
    static_assert(std::derived_from<try_apply_async_closure, ex::sender_adaptor_closure<try_apply_async_closure>>);
    static_assert(std::derived_from<apply_shared_closure, ex::sender_adaptor_closure<apply_shared_closure>>);
    static_assert(
        std::derived_from<apply_shared_async_closure, ex::sender_adaptor_closure<apply_shared_async_closure>>);
    static_assert(std::derived_from<try_apply_shared_closure, ex::sender_adaptor_closure<try_apply_shared_closure>>);
    static_assert(
        std::derived_from<try_apply_shared_async_closure, ex::sender_adaptor_closure<try_apply_shared_async_closure>>);
}

TEST(SerializedAccessTest, SharedApplyCallbacksCanOverlap) {
    // Arrange
    serialized_access<int, shared_serial_gate> sut{std::in_place, 0};
    std::binary_semaphore                      first_started{0};
    std::binary_semaphore                      release_first{0};
    std::binary_semaphore                      second_started{0};
    std::atomic<bool>                          second_invoked{false};

    // Act
    std::thread first{[&] {
        ex::sync_wait(ex::just() | sut.apply_shared_async([&](const int&) {
            first_started.release();
            return ex::just() | ex::then([&] { release_first.acquire(); });
        }));
    }};
    first_started.acquire();

    std::thread second{[&] {
        ex::sync_wait(ex::just() | sut.apply_shared([&](const int&) {
            second_invoked.store(true, std::memory_order_relaxed);
            second_started.release();
        }));
    }};

    second_started.acquire();

    // Assert
    EXPECT_TRUE(second_invoked.load(std::memory_order_relaxed));

    release_first.release();
    first.join();
    second.join();
}

TEST(SerializedAccessTest, ExclusiveApplyExcludesSharedApply) {
    // Arrange
    serialized_access<int, shared_serial_gate> sut{std::in_place, 0};
    std::binary_semaphore                      writer_started{0};
    std::binary_semaphore                      release_writer{0};
    std::atomic<bool>                          shared_invoked{false};

    // Act
    std::thread writer{[&] {
        ex::sync_wait(ex::just() | sut.apply_async([&](int&) {
            writer_started.release();
            return ex::just() | ex::then([&] { release_writer.acquire(); });
        }));
    }};
    writer_started.acquire();

    std::thread shared{[&] {
        ex::sync_wait(ex::just() |
                      sut.apply_shared([&](const int&) { shared_invoked.store(true, std::memory_order_relaxed); }));
    }};

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Assert
    EXPECT_FALSE(shared_invoked.load(std::memory_order_relaxed));

    release_writer.release();
    writer.join();
    shared.join();
    EXPECT_TRUE(shared_invoked.load(std::memory_order_relaxed));
}

TEST(SerializedAccessTest, SharedApplyAsyncHoldsSharedAccessUntilCompletion) {
    // Arrange
    serialized_access<int, shared_serial_gate> sut{std::in_place, 0};
    std::binary_semaphore                      shared_started{0};
    std::binary_semaphore                      release_shared{0};
    std::atomic<bool>                          exclusive_invoked{false};

    // Act
    std::thread shared{[&] {
        ex::sync_wait(ex::just() | sut.apply_shared_async([&](const int&) {
            shared_started.release();
            return ex::just() | ex::then([&] { release_shared.acquire(); });
        }));
    }};
    shared_started.acquire();

    std::thread exclusive{[&] {
        ex::sync_wait(ex::just() | sut.apply_async([&](int&) {
            exclusive_invoked.store(true, std::memory_order_relaxed);
            return ex::just();
        }));
    }};

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Assert
    EXPECT_FALSE(exclusive_invoked.load(std::memory_order_relaxed));

    release_shared.release();
    shared.join();
    exclusive.join();
    EXPECT_TRUE(exclusive_invoked.load(std::memory_order_relaxed));
}

TEST(SerializedAccessTest, ApplyAsyncHoldsAccessUntilReturnedSenderCompletes) {

    // Arrange
    serialized_access<int> sut{std::in_place, 0};
    std::binary_semaphore  first_started{0};
    std::binary_semaphore  release_first{0};
    std::binary_semaphore  second_ready{0};
    std::atomic<bool>      second_invoked{false};

    // Act
    std::thread first{[&] {
        ex::sync_wait(ex::just() | sut.apply_async([&](int&) {
            first_started.release();
            release_first.acquire();
            return ex::just();
        }));
    }};
    first_started.acquire();

    std::thread second{[&] {
        second_ready.release();
        ex::sync_wait(ex::just() | sut.apply_async([&](int&) {
            second_invoked.store(true);
            return ex::just();
        }));
    }};

    second_ready.acquire();

    // Assert the second callback cannot run while the first returned sender is incomplete.
    EXPECT_FALSE(second_invoked.load());

    release_first.release();
    first.join();
    second.join();

    // Assert
    EXPECT_TRUE(second_invoked.load());
}

TEST(SerializedAccessTest, TryAccessReportsBusyWithoutInvokingCallbacks) {

    // Arrange
    serialized_access<int> sut{std::in_place, 0};
    std::binary_semaphore  holder_started{0};
    std::binary_semaphore  release_holder{0};
    std::atomic<bool>      try_apply_invoked{false};
    std::atomic<bool>      try_apply_async_invoked{false};

    std::thread holder{[&] {
        ex::sync_wait(ex::just() | sut.apply_async([&](int&) {
            holder_started.release();
            release_holder.acquire();
            return ex::just();
        }));
    }};
    holder_started.acquire();

    // Act
    bool try_apply_busy{false};
    try {
        ex::sync_wait(ex::just() | sut.try_apply([&](int&) { try_apply_invoked.store(true); }));
    } catch (const busy_error&) {
        try_apply_busy = true;
    }

    bool try_apply_async_busy{false};
    try {
        ex::sync_wait(ex::just() | sut.try_apply_async([&](int&) {
            try_apply_async_invoked.store(true);
            return ex::just();
        }));
    } catch (const busy_error&) {
        try_apply_async_busy = true;
    }

    release_holder.release();
    holder.join();

    // Assert
    EXPECT_TRUE(try_apply_busy);
    EXPECT_TRUE(try_apply_async_busy);
    EXPECT_FALSE(try_apply_invoked.load());
    EXPECT_FALSE(try_apply_async_invoked.load());
}

TEST(SerializedAccessTest, TrySharedAccessReportsBusyWithoutInvokingCallbacks) {
    // Arrange
    serialized_access<int, shared_serial_gate> sut{std::in_place, 0};
    std::binary_semaphore                      holder_started{0};
    std::binary_semaphore                      release_holder{0};
    std::atomic<bool>                          try_invoked{false};
    std::atomic<bool>                          try_async_invoked{false};

    // Act
    std::thread holder{[&] {
        ex::sync_wait(ex::just() | sut.apply_async([&](int&) {
            holder_started.release();
            return ex::just() | ex::then([&] { release_holder.acquire(); });
        }));
    }};
    holder_started.acquire();

    bool try_busy{false};
    try {
        ex::sync_wait(ex::just() | sut.try_apply_shared([&](const int&) { try_invoked.store(true); }));
    } catch (const busy_error&) {
        try_busy = true;
    }

    bool try_async_busy{false};
    try {
        ex::sync_wait(ex::just() | sut.try_apply_shared_async([&](const int&) {
            try_async_invoked.store(true);
            return ex::just();
        }));
    } catch (const busy_error&) {
        try_async_busy = true;
    }

    release_holder.release();
    holder.join();

    // Assert
    EXPECT_TRUE(try_busy);
    EXPECT_TRUE(try_async_busy);
    EXPECT_FALSE(try_invoked.load());
    EXPECT_FALSE(try_async_invoked.load());
}

TEST(SerializedAccessTest, ApplyPropagatesCallbackErrors) {

    // Arrange
    serialized_access<int> sut{std::in_place, 0};

    // Act and assert
    EXPECT_THROW(
        ex::sync_wait(ex::just() | sut.apply([](int&) -> void { throw std::runtime_error("callback failed"); })),
        std::runtime_error);
}
