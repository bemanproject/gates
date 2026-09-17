// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_GATES_DETAIL_RW_TASK_QUEUE_HPP
#define BEMAN_GATES_DETAIL_RW_TASK_QUEUE_HPP

#include <beman/gates/config.hpp>

#if BEMAN_GATES_USE_MODULES() && !defined(BEMAN_GATES_INCLUDED_FROM_INTERFACE_UNIT)
import beman.gates;
#else
    #include <beman/gates/detail/synchronized_value.hpp>
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

/// A simple, low-level reader/writer task queue.
///
/// Shared tasks can execute concurrently. Exclusive tasks execute one at a time and exclude shared tasks. The queue
/// gives preference to exclusive tasks: once an exclusive task is waiting, newly enqueued shared tasks wait as well.
///
/// - Requires: all tasks accepted by `enqueue()` or `try_enqueue()` need to be completed before the queue is
///   destroyed.
/// - Note: the order of execution of tasks is not guaranteed.
struct rw_task_queue {
    /// The access mode requested by a task.
    enum class access { exclusive, shared };

    rw_task_queue() = default;
    ~rw_task_queue() {
        data_.apply([&](queue_data& data) noexcept {
            if (data.active_readers_ != 0 || data.active_writer_ || data.waiting_readers_count_ != 0 ||
                data.waiting_writers_count_ != 0) {
                std::terminate();
            }
        });
    }

    rw_task_queue(const rw_task_queue&)            = delete;
    rw_task_queue& operator=(const rw_task_queue&) = delete;

    /// Adds `t` to the execution queue with the requested access mode.
    ///
    /// If `t` requests shared access and no exclusive task is active or waiting, it starts immediately and may execute
    /// concurrently with other shared tasks. Otherwise, `t` will be started after the currently active tasks complete
    /// and after any waiting exclusive tasks have been admitted.
    ///
    /// If `t` requests exclusive access, it starts immediately only when the queue is idle and has no pending tasks.
    /// Otherwise, it will be started after all active tasks complete and before pending shared tasks.
    ///
    /// - Requires: `t` must not be in the queue already.
    /// - Requires: `on_task_complete()` must be called after the task completes.
    /// - Requires: `t` must not be destroyed before `on_task_complete()` is called for it.
    void enqueue(task_base* t, access requested_access) noexcept {
        task_base* ready = nullptr;
        data_.apply([&](queue_data& data) noexcept {
            if (can_start(requested_access, data)) {
                activate(requested_access, data);
                ready = t;
            } else {
                add_pending(t, requested_access, data);
            }
        });
        start_tasks(ready);
    }

    /// Attempts to reserve the queue for `t` and start it immediately.
    ///
    /// A shared task succeeds when no exclusive task is active or waiting, including when other shared tasks are
    /// active. An exclusive task succeeds only when the queue is idle. Otherwise, this returns `false` without
    /// starting `t`.
    ///
    /// - Requires: `t` must not be in the queue already.
    /// - Requires: if this returns `true`, `on_task_complete()` must be called after the task completes.
    /// - Requires: if this returns `true`, `t` must not be destroyed before `on_task_complete()` is called for it.
    [[nodiscard]] bool try_enqueue(task_base* t, access requested_access) noexcept {
        bool accepted = false;
        data_.apply([&](queue_data& data) noexcept {
            accepted = can_start(requested_access, data);
            if (accepted) {
                activate(requested_access, data);
            }
        });

        if (accepted) {
            start_tasks(t);
        }
        return accepted;
    }

    /// Notifies the queue that a task accepted by `enqueue()` or `try_enqueue()` has completed.
    ///
    /// If the completed task was shared and other shared tasks remain active, no task is started. Once the queue
    /// becomes idle, one pending exclusive task is started before any pending shared tasks. If no exclusive task is
    /// waiting, all pending shared tasks are started together.
    ///
    /// - Requires: `enqueue()` must have been called before the task started executing, or `try_enqueue()` must have
    ///   returned `true` for the task.
    void on_task_complete(access completed_access) noexcept {
        task_base* ready = nullptr;
        data_.apply([&](queue_data& data) noexcept {
            if (completed_access == access::shared) {
                --data.active_readers_;
            } else {
                data.active_writer_ = false;
            }

            if (!data.active_writer_ && data.active_readers_ == 0) {
                if (data.waiting_writers_count_ != 0) {
                    ready = pop(data.waiting_writers_);
                    --data.waiting_writers_count_;
                    data.active_writer_ = true;
                } else if (data.waiting_readers_count_ != 0) {
                    ready                       = data.waiting_readers_;
                    data.waiting_readers_       = nullptr;
                    data.active_readers_        = data.waiting_readers_count_;
                    data.waiting_readers_count_ = 0;
                }
            }
        });
        start_tasks(ready);
    }

  private:
    /// The queue state.
    struct queue_data {
        /// Number of currently active shared tasks.
        std::size_t active_readers_{0};
        /// Whether an exclusive task is currently active.
        bool active_writer_{false};
        /// Pending shared-access tasks.
        task_base* waiting_readers_{nullptr};
        /// Pending exclusive-access tasks.
        task_base* waiting_writers_{nullptr};
        /// Number of pending shared-access tasks.
        std::size_t waiting_readers_count_{0};
        /// Number of pending exclusive-access tasks.
        std::size_t waiting_writers_count_{0};
    };

    /// The synchronized queue state.
    synchronized_value<queue_data> data_{::std::in_place};

    /// Returns whether an access can start immediately under the writer-preference policy in `data`.
    [[nodiscard]] static bool can_start(access requested_access, const queue_data& data) noexcept {
        if (requested_access == access::shared) {
            return !data.active_writer_ && data.waiting_writers_count_ == 0;
        }
        return !data.active_writer_ && data.active_readers_ == 0 && data.waiting_readers_count_ == 0 &&
               data.waiting_writers_count_ == 0;
    }

    /// Marks an immediately admitted access as active in `data`.
    static void activate(access requested_access, queue_data& data) noexcept {
        if (requested_access == access::shared) {
            ++data.active_readers_;
        } else {
            data.active_writer_ = true;
        }
    }

    /// Adds a task to the pending list in `data` for `requested_access`.
    static void add_pending(task_base* task, access requested_access, queue_data& data) noexcept {
        if (requested_access == access::shared) {
            push(data.waiting_readers_, task);
            ++data.waiting_readers_count_;
        } else {
            push(data.waiting_writers_, task);
            ++data.waiting_writers_count_;
        }
    }

    /// Adds `task` to the head of an intrusive pending list.
    static void push(task_base*& head, task_base* task) noexcept {
        task->next_ = head;
        head        = task;
    }

    /// Removes and returns the head of an intrusive pending list.
    static task_base* pop(task_base*& head) noexcept {
        task_base* result = head;
        head              = result->next_;
        result->next_     = nullptr;
        return result;
    }

    /// Starts all tasks in the supplied list. The list is detached from the queue before this is called.
    static void start_tasks(task_base* task) noexcept {
        while (task != nullptr) {
            task_base* next = task->next_;
            task->execute_(task);
            task = next;
        }
    }
};

} // namespace beman::gates::detail

#endif
