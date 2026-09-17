// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_GATES_DETAIL_RW_TASK_QUEUE_RW_TASK_QUEUE_HPP
#define BEMAN_GATES_DETAIL_RW_TASK_QUEUE_RW_TASK_QUEUE_HPP

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
        if (active_readers_ != 0 || active_writer_ || waiting_readers_count_ != 0 || waiting_writers_count_ != 0) {
            std::terminate();
        }
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
        lock();

        if (can_start(requested_access)) {
            activate(requested_access);
            ready = t;
        } else {
            add_pending(t, requested_access);
        }

        unlock();
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
        lock();
        const bool accepted = can_start(requested_access);
        if (accepted) {
            activate(requested_access);
        }
        unlock();

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
        lock();

        if (completed_access == access::shared) {
            --active_readers_;
        } else {
            active_writer_ = false;
        }

        if (!active_writer_ && active_readers_ == 0) {
            if (waiting_writers_count_ != 0) {
                ready = pop(waiting_writers_);
                --waiting_writers_count_;
                active_writer_ = true;
            } else if (waiting_readers_count_ != 0) {
                ready                  = waiting_readers_;
                waiting_readers_       = nullptr;
                active_readers_        = waiting_readers_count_;
                waiting_readers_count_ = 0;
            }
        }

        unlock();
        start_tasks(ready);
    }

  private:
    /// Returns whether an access can start immediately under the writer-preference policy.
    ///
    /// - Requires: The caller must hold the queue lock.
    [[nodiscard]] bool can_start(access requested_access) const noexcept {
        if (requested_access == access::shared) {
            return !active_writer_ && waiting_writers_count_ == 0;
        }
        return !active_writer_ && active_readers_ == 0 && waiting_readers_count_ == 0 && waiting_writers_count_ == 0;
    }

    /// Marks an immediately admitted access as active.
    ///
    /// - Requires: The caller must hold the queue lock.
    void activate(access requested_access) noexcept {
        if (requested_access == access::shared) {
            ++active_readers_;
        } else {
            active_writer_ = true;
        }
    }

    /// Adds a task to the pending list for `requested_access`.
    ///
    /// - Requires: The caller must hold the queue lock.
    void add_pending(task_base* task, access requested_access) noexcept {
        if (requested_access == access::shared) {
            push(waiting_readers_, task);
            ++waiting_readers_count_;
        } else {
            push(waiting_writers_, task);
            ++waiting_writers_count_;
        }
    }

    /// Adds `task` to the head of an intrusive pending list.
    ///
    /// - Requires: The caller must hold the queue lock.
    static void push(task_base*& head, task_base* task) noexcept {
        task->next_ = head;
        head        = task;
    }

    /// Removes and returns the head of an intrusive pending list.
    ///
    /// - Requires: The caller must hold the queue lock.
    static task_base* pop(task_base*& head) noexcept {
        task_base* result = head;
        head              = result->next_;
        result->next_     = nullptr;
        return result;
    }

    /// Starts all tasks in the supplied list. The list is detached from the queue before this is called.
    ///
    /// - Note: Should be called outside of the queue lock.
    static void start_tasks(task_base* task) noexcept {
        while (task != nullptr) {
            task_base* next = task->next_;
            task->execute_(task);
            task = next;
        }
    }

    /// Acquires the queue lock.
    ///
    /// - Requires: The caller must not already hold the queue lock.
    void lock() noexcept {
        while (lock_.test_and_set(std::memory_order_acquire)) {
            lock_.wait(true, std::memory_order_relaxed);
        }
    }

    /// Releases the queue lock.
    ///
    /// - Requires: The caller must hold the queue lock.
    void unlock() noexcept {
        lock_.clear(std::memory_order_release);
        lock_.notify_one();
    }

  private:
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
    /// Protects the queue state. Critical sections never execute user code.
    std::atomic_flag lock_{};
};

} // namespace beman::gates::detail

#endif
