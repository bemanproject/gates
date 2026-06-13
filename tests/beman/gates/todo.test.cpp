// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/gates/config.hpp>
#include <gtest/gtest.h>
#include <beman/gates/todo.hpp>

TEST(TodoTest, todo) {
    const bool todo = true;
    EXPECT_TRUE(todo);
}
