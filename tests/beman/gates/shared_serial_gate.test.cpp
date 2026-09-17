// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/gates/shared_serial_gate.hpp>
#include <beman/execution/execution.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <concepts>
#include <future>
#include <semaphore>
#include <thread>

namespace ex = beman::execution;
using namespace beman::gates;

TEST(SharedSerialGateTest, AcquisitionFunctionsReturnScopes) {
    // Arrange
    using acquire_t            = decltype(shared_serial_gate{}.acquire());
    using try_acquire_t        = decltype(shared_serial_gate{}.try_acquire());
    using acquire_shared_t     = decltype(shared_serial_gate{}.acquire_shared());
    using try_acquire_shared_t = decltype(shared_serial_gate{}.try_acquire_shared());

    // Act and Assert
    static_assert(ex::enter_scope_sender_in<acquire_t>);
    static_assert(ex::enter_scope_sender_in<try_acquire_t>);
    static_assert(ex::enter_scope_sender_in<acquire_shared_t>);
    static_assert(ex::enter_scope_sender_in<try_acquire_shared_t>);
}

TEST(SharedSerialGateTest, IsNonCopyableAndNonMovable) {
    // Act and Assert
    static_assert(!std::copy_constructible<shared_serial_gate>);
    static_assert(!std::move_constructible<shared_serial_gate>);
}

TEST(SharedSerialGateTest, SharedAccessesCanOverlap) {
    // Arrange
    shared_serial_gate    sut;
    std::binary_semaphore first_started{0};
    std::binary_semaphore release_first{0};
    std::binary_semaphore second_started{0};
    std::atomic<bool>     second_invoked{false};

    // Act
    std::thread first{[&] {
        ex::sync_wait(ex::within(sut.try_acquire_shared(), ex::just() | ex::then([&] {
                                                               first_started.release();
                                                               release_first.acquire();
                                                           })));
    }};
    first_started.acquire();

    std::thread second{[&] {
        ex::sync_wait(ex::within(sut.acquire_shared(), ex::just() | ex::then([&] {
                                                           second_invoked.store(true, std::memory_order_relaxed);
                                                           second_started.release();
                                                       })));
    }};

    second_started.acquire();

    // Assert
    EXPECT_TRUE(second_invoked.load(std::memory_order_relaxed));

    release_first.release();
    first.join();
    second.join();
}

TEST(SharedSerialGateTest, WriterPreferenceBlocksReadersBehindWaitingWriter) {
    // Arrange
    shared_serial_gate    sut;
    std::binary_semaphore holder_started{0};
    std::binary_semaphore release_holder{0};
    std::binary_semaphore writer_started{0};
    std::binary_semaphore release_writer{0};
    std::atomic<bool>     later_reader_invoked{false};

    // Act
    std::thread holder{[&] {
        ex::sync_wait(ex::within(sut.acquire_shared(), ex::just() | ex::then([&] {
                                                           holder_started.release();
                                                           release_holder.acquire();
                                                       })));
    }};
    holder_started.acquire();

    std::thread writer{[&] {
        ex::sync_wait(ex::within(sut.acquire(), ex::just() | ex::then([&] {
                                                    writer_started.release();
                                                    release_writer.acquire();
                                                })));
    }};

    // A shared try-acquisition succeeds while only the holder is active, and fails once the writer is pending.
    bool       writer_is_pending{false};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline && !writer_is_pending) {
        try {
            ex::sync_wait(ex::within(sut.try_acquire_shared(), ex::just()));
        } catch (const busy_error&) {
            writer_is_pending = true;
        }
        std::this_thread::yield();
    }
    ASSERT_TRUE(writer_is_pending);

    std::thread later_reader{[&] {
        ex::sync_wait(ex::within(sut.acquire_shared(), ex::just() | ex::then([&] {
                                                           later_reader_invoked.store(true, std::memory_order_relaxed);
                                                       })));
    }};

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    // Assert
    EXPECT_FALSE(later_reader_invoked.load(std::memory_order_relaxed));

    release_holder.release();
    writer_started.acquire();
    EXPECT_FALSE(later_reader_invoked.load(std::memory_order_relaxed));

    release_writer.release();
    holder.join();
    writer.join();
    later_reader.join();

    EXPECT_TRUE(later_reader_invoked.load(std::memory_order_relaxed));
}

TEST(SharedSerialGateTest, ExclusiveAccessExcludesSharedAndExclusiveAccesses) {
    // Arrange
    shared_serial_gate    sut;
    std::binary_semaphore writer_started{0};
    std::binary_semaphore release_writer{0};
    std::atomic<bool>     shared_invoked{false};
    std::atomic<bool>     second_writer_invoked{false};

    // Act
    std::thread writer{[&] {
        ex::sync_wait(ex::within(sut.acquire(), ex::just() | ex::then([&] {
                                                    writer_started.release();
                                                    release_writer.acquire();
                                                })));
    }};
    writer_started.acquire();

    std::thread shared{[&] {
        ex::sync_wait(ex::within(sut.acquire_shared(), ex::just() | ex::then([&] {
                                                           shared_invoked.store(true, std::memory_order_relaxed);
                                                       })));
    }};
    std::thread second_writer{[&] {
        ex::sync_wait(ex::within(sut.acquire(), ex::just() | ex::then([&] {
                                                    second_writer_invoked.store(true, std::memory_order_relaxed);
                                                })));
    }};

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    // Assert
    EXPECT_FALSE(shared_invoked.load(std::memory_order_relaxed));
    EXPECT_FALSE(second_writer_invoked.load(std::memory_order_relaxed));

    release_writer.release();
    writer.join();
    shared.join();
    second_writer.join();

    EXPECT_TRUE(shared_invoked.load(std::memory_order_relaxed));
    EXPECT_TRUE(second_writer_invoked.load(std::memory_order_relaxed));
}

TEST(SharedSerialGateTest, TryAccessReportsBusyForIncompatibleAccess) {
    // Arrange
    shared_serial_gate    sut;
    std::binary_semaphore shared_started{0};
    std::binary_semaphore release_shared{0};

    // Act
    std::thread shared{[&] {
        ex::sync_wait(ex::within(sut.acquire_shared(), ex::just() | ex::then([&] {
                                                           shared_started.release();
                                                           release_shared.acquire();
                                                       })));
    }};
    shared_started.acquire();

    bool exclusive_busy{false};
    try {
        ex::sync_wait(ex::within(sut.try_acquire(), ex::just()));
    } catch (const busy_error&) {
        exclusive_busy = true;
    }
    // Assert
    EXPECT_TRUE(exclusive_busy);

    release_shared.release();
    shared.join();

    std::binary_semaphore writer_started{0};
    std::binary_semaphore release_writer{0};
    std::thread           writer{[&] {
        ex::sync_wait(ex::within(sut.acquire(), ex::just() | ex::then([&] {
                                                    writer_started.release();
                                                    release_writer.acquire();
                                                })));
    }};
    writer_started.acquire();

    bool shared_busy{false};
    try {
        ex::sync_wait(ex::within(sut.try_acquire_shared(), ex::just()));
    } catch (const busy_error&) {
        shared_busy = true;
    }
    EXPECT_TRUE(shared_busy);

    release_writer.release();
    writer.join();

    bool exclusive_succeeded{false};
    ex::sync_wait(ex::within(sut.try_acquire(), ex::just() | ex::then([&] { exclusive_succeeded = true; })));
    EXPECT_TRUE(exclusive_succeeded);
}
