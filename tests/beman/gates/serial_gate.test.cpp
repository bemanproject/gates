// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/gates/serial_gate.hpp>
#include <beman/execution/execution.hpp>

#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <chrono>

namespace ex = beman::execution;
using namespace beman::gates;

TEST(SerialGateTest, SerialGateAcquireReturnsAScope) {
    using t = decltype(serial_gate{}.acquire());
    static_assert(ex::enter_scope_sender_in<t>, "serial_gate::acquire() should return a scope sender");
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

/// Check that multiple items are executed, assuming there is no data race between them.
TEST(SerialGateTest, SerialGateCanExecuteMultipleWorkItems) {

    // Arrange
    bool            executed = false;
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
    bool             executed = false;
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
