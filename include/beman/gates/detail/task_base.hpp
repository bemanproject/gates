// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_GATES_DETAIL_TASK_BASE_HPP
#define BEMAN_GATES_DETAIL_TASK_BASE_HPP

namespace beman::gates::detail {

/// Base class for tasks.
///
/// Instead of having a virtual function, we store the pointer to the function that executes the task as a data member.
/// This is to avoid an extra vtable pointer.
///
/// This has a `next_` member to allow building intrusive linked lists of tasks.
struct task_base {
    /// Function that executes the task.
    void (*execute_)(task_base*) noexcept = nullptr;
    /// The next task in the queue.
    task_base* next_ = nullptr;
};

} // namespace beman::gates::detail

#endif
