// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_GATES_DETAIL_TASK_QUEUE_HPP
#define BEMAN_GATES_DETAIL_TASK_QUEUE_HPP

#include <beman/gates/config.hpp>

#if BEMAN_GATES_USE_MODULES() && !defined(BEMAN_GATES_INCLUDED_FROM_INTERFACE_UNIT)
import beman.gates;
#else
    #include <beman/gates/detail/task_base.hpp>
#endif

#ifdef BEMAN_HAS_IMPORT_STD
import std;
#else
    #include <atomic>
    #include <cstddef>
    #include <exception>
#endif

namespace beman::gates::detail {

/// A simple, low-level task queue.
///
/// - Requires: all tasks enqueued need to be completed before the queue is destroyed.
/// - Note: the order of execution of tasks is not guaranteed.
struct task_queue {

    task_queue() = default;
    ~task_queue() {
        if (count_.load(::std::memory_order_acquire) > 0) {
            std::terminate();
        }
    }

    /// Adds `t` to the execution queue.
    ///
    /// If the queue is empty, starts the task immediately. Otherwise, the task will be started after the currently
    /// executing task completes.
    ///
    /// - Requires: `t` must not be in the queue already.
    /// - Requires: `on_task_complete()` must be called after the task completes.
    /// - Requires: `t` must not be destroyed before `on_task_complete()` is called for it.
    void enqueue(task_base* t) {
        t->next_ = head_.load(std::memory_order_relaxed);
        while (!head_.compare_exchange_weak(t->next_, t, std::memory_order_release, std::memory_order_relaxed)) {
        }

        if (count_.fetch_add(1, std::memory_order_acq_rel) == 0) {
            start_next_task();
        }
    }

    /// Notifies the queue that a task has completed.
    ///
    /// - Requires: `enqueue()` must have been called before the task started executing.
    void on_task_complete() {
        if (count_.fetch_sub(1, ::std::memory_order_acq_rel) > 1) {
            start_next_task();
        }
    }

  private:
    /// Pops the next task from the queue; returns `nullptr` if the queue is empty.
    task_base* pop() noexcept {
        auto* old = head_.load(std::memory_order_acquire);
        while (old != nullptr) {
            auto* next = old->next_;
            if (head_.compare_exchange_weak(old, next, std::memory_order_acquire, std::memory_order_relaxed)) {
                return old;
            }
        }
        return nullptr;
    }

    /// Starts executing the next task in the queue.
    void start_next_task() {
        task_base* next;
        while ((next = pop()) == nullptr) {
            // Should never happen.
            // `count_` says a task exists, but we increment `count_` after we enqueue the task.
        }
        next->execute_(next);
    }

  private:
    /// Scheduling counter for outstanding tasks; includes the task currently executing.
    /// This is incremented after publishing a task to `head_`, so it can be transiently
    /// lower than the number of visible queued/running tasks.
    std::atomic<size_t> count_{0};
    /// The head of the queue of tasks that are waiting to be executed.
    std::atomic<task_base*> head_{nullptr};
};

} // namespace beman::gates::detail

#endif
