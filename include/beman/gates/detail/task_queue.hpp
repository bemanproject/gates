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
    #include <mutex>
    #include <vector>
#endif

namespace beman::gates::detail {

/// A simple, low-level task queue.
///
/// - Requires: all tasks enqueued need to be completed before the queue is destroyed.
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
    /// - Requires: `on_task_complete()` must be called after the task completes.
    void enqueue(task_base* t) {
        {
            std::lock_guard lock{queue_bottleneck_};
            queue_.emplace_back(t);
        }
        if (count_.fetch_add(1, ::std::memory_order_acq_rel) == 0) {
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
    /// Starts executing the next task in the queue.
    void start_next_task() {
        task_base* next_task;
        {
            std::lock_guard lock{queue_bottleneck_};
            next_task = queue_.front();
            queue_.erase(queue_.begin());
        }
        next_task->execute_(next_task);
    }

  private:
    /// The number of tasks that are submitted to the gate; includes the task that is currently executing.
    std::atomic<size_t> count_{0};
    /// Mutex protecting the queue of tasks.
    std::mutex queue_bottleneck_;
    /// The queue of tasks that are waiting to be executed.
    std::vector<task_base*> queue_;

    // TODO: better implementation
};

} // namespace beman::gates::detail

#endif
