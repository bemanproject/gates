// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/gates/serial_gate.hpp>
#include <beman/execution/execution.hpp>

#include <gtest/gtest.h>

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
