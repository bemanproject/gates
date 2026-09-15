// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/gates/serial_gate.hpp>
#include <beman/execution/execution.hpp>

#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <future>
#include <semaphore>

namespace ex = beman::execution;
using namespace beman::gates;

TEST(SerialGateTest, SerialGateAcquireReturnsAScope) {
    using t = decltype(serial_gate{}.acquire());
    static_assert(ex::enter_scope_sender_in<t>, "serial_gate::acquire() should return a scope sender");
}

TEST(SerialGateTest, SerialGateTryAcquireReturnsAScope) {
    using t = decltype(serial_gate{}.try_acquire());
    static_assert(ex::enter_scope_sender_in<t>, "serial_gate::try_acquire() should return a scope sender");
}

TEST(SerialGateTest, SerialGateCanExecuteWorkWithinItsScope) {

    // Arrange
    bool            executed = false;
    ex::sender auto work     = ex::just() | ex::then([&executed]() noexcept { executed = true; });
    serial_gate     sut;

    // Act
    ex::sync_wait(ex::within(sut.acquire(), std::move(work)));

    // Assert
    EXPECT_TRUE(executed);
}

TEST(SerialGateTest, SerialGateCanExecuteWorkWithinTryScope) {

    // Arrange
    bool            executed = false;
    ex::sender auto work     = ex::just() | ex::then([&executed]() noexcept { executed = true; });
    serial_gate     sut;

    // Act
    ex::sync_wait(ex::within(sut.try_acquire(), std::move(work)));

    // Assert
    EXPECT_TRUE(executed);
}

TEST(SerialGateTest, SerialGateTryAcquireFailsWhenBusy) {

    // Arrange
    serial_gate           sut;
    std::binary_semaphore holder_started{0};
    std::binary_semaphore release_holder{0};
    std::atomic<bool>     speculative_executed{false};
    std::promise<void>    contender_done_promise;
    auto                  contender_done = contender_done_promise.get_future();
    ex::sender auto       holder_work    = ex::just() | ex::then([&]() noexcept {
                                      holder_started.release();
                                      release_holder.acquire();
                                           });

    // Act
    std::thread holder_thread{[&] { ex::sync_wait(ex::within(sut.acquire(), holder_work)); }};
    holder_started.acquire();

    bool        caught_busy{false};
    bool        caught_unexpected_error{false};
    std::thread contender_thread{[&] {
        try {
            auto speculative_work =
                ex::just() | ex::then([&]() noexcept { speculative_executed.store(true, std::memory_order_relaxed); });
            ex::sync_wait(ex::within(sut.try_acquire(), std::move(speculative_work)));
        } catch (const busy_error&) {
            caught_busy = true;
        } catch (...) {
            caught_unexpected_error = true;
        }
        contender_done_promise.set_value();
    }};

    const bool finished_while_busy = contender_done.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
    release_holder.release();
    holder_thread.join();
    contender_thread.join();

    // Assert
    EXPECT_TRUE(finished_while_busy);
    EXPECT_TRUE(caught_busy);
    EXPECT_FALSE(caught_unexpected_error);
    EXPECT_FALSE(speculative_executed.load(std::memory_order_relaxed));
}

/// Check that multiple items are executed, assuming there is no data race between them.
TEST(SerialGateTest, SerialGateCanExecuteMultipleWorkItems) {

    // Arrange
    int             counter{0};
    ex::sender auto work = ex::just() | ex::then([&counter]() noexcept { ++counter; });
    serial_gate     sut;
    auto            thread_work = [&]() { ex::sync_wait(ex::within(sut.acquire(), work)); };

    // Act
    std::thread t1{thread_work};
    std::thread t2{thread_work};
    std::thread t3{thread_work};
    t1.join();
    t2.join();
    t3.join();

    // Assert
    EXPECT_EQ(counter, 3);
}

/// Check that multiple items are executed serially, ensuring there is no overlap
TEST(SerialGateTest, SerialGateCanExecuteMultipleWorkItemsSerially) {

    // Arrange
    std::atomic<int> counter{0};
    ex::sender auto  work = ex::just() | ex::then([&counter]() noexcept {
                               const int value = counter.fetch_add(1, std::memory_order_relaxed);
                               std::this_thread::sleep_for(std::chrono::milliseconds(00));
                               const int value2 = counter.load(std::memory_order_relaxed);
                               EXPECT_EQ(value + 1, value2);
                            });
    serial_gate      sut;
    auto             thread_work = [&]() { ex::sync_wait(ex::within(sut.acquire(), work)); };

    // Act
    std::thread t1{thread_work};
    std::thread t2{thread_work};
    std::thread t3{thread_work};
    t1.join();
    t2.join();
    t3.join();

    // Assert
    EXPECT_EQ(counter.load(std::memory_order_relaxed), 3);
}
