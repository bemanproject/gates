// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_GATES_SHARED_SERIAL_GATE_HPP
#define BEMAN_GATES_SHARED_SERIAL_GATE_HPP

#include <beman/gates/config.hpp>

#if BEMAN_GATES_USE_MODULES() && !defined(BEMAN_GATES_INCLUDED_FROM_INTERFACE_UNIT)
import beman.gates;
import beman.execution;
#else
    #include <beman/gates/detail/rw_task_queue.hpp>
    #include <beman/gates/detail/scope_over_queue.hpp>

    #include <beman/execution/execution.hpp>
#endif

namespace beman::gates {

namespace detail {

/// Trait that wraps a `rw_task_queue` and uses it as `Access` in a non-speculative manner.
template <rw_task_queue::access Access>
struct rw_queue_access_trait {
    using queue_type                  = rw_task_queue;
    static constexpr bool speculative = false;

    static void enqueue(queue_type* queue, task_base* task) { queue->enqueue(task, Access); }
    static void on_scope_complete(queue_type* queue) { queue->on_task_complete(Access); }
};

/// Trait that wraps a `rw_task_queue` and uses it as `Access`  in a speculative manner.
template <rw_task_queue::access Access>
struct rw_speculative_queue_access_trait {
    using queue_type                  = rw_task_queue;
    static constexpr bool speculative = true;

    static bool enqueue(queue_type* queue, task_base* task) { return queue->try_enqueue(task, Access); }
    static void on_scope_complete(queue_type* queue) { queue->on_task_complete(Access); }
};

} // namespace detail

/// A gate that allows concurrent shared work while excluding exclusive work.
struct shared_serial_gate {
    shared_serial_gate()  = default;
    ~shared_serial_gate() = default;

    shared_serial_gate(const shared_serial_gate&) = delete;
    shared_serial_gate(shared_serial_gate&&)      = delete;

    /// Returns an enter-scope sender that acquires exclusive access through `*this`.
    [[nodiscard]] inline ::beman::execution::enter_scope_sender auto acquire() noexcept {
        return detail::scope_over_queue<detail::rw_queue_access_trait<detail::rw_task_queue::access::exclusive>>(
            &queue_);
    }

    /// Returns an enter-scope sender that tries to acquire exclusive access without waiting.
    [[nodiscard]] inline ::beman::execution::enter_scope_sender auto try_acquire() noexcept {
        return detail::scope_over_queue<
            detail::rw_speculative_queue_access_trait<detail::rw_task_queue::access::exclusive>>(&queue_);
    }

    /// Returns an enter-scope sender that acquires shared access through `*this`.
    [[nodiscard]] inline ::beman::execution::enter_scope_sender auto acquire_shared() noexcept {
        return detail::scope_over_queue<detail::rw_queue_access_trait<detail::rw_task_queue::access::shared>>(&queue_);
    }

    /// Returns an enter-scope sender that tries to acquire shared access without waiting.
    [[nodiscard]] inline ::beman::execution::enter_scope_sender auto try_acquire_shared() noexcept {
        return detail::scope_over_queue<
            detail::rw_speculative_queue_access_trait<detail::rw_task_queue::access::shared>>(&queue_);
    }

  private:
    /// Queue that schedules shared and exclusive work.
    detail::rw_task_queue queue_;
};

} // namespace beman::gates

#endif
