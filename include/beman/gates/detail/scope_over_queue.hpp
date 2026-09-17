// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_GATES_DETAIL_SCOPE_OVER_QUEUE_HPP
#define BEMAN_GATES_DETAIL_SCOPE_OVER_QUEUE_HPP

#include <beman/gates/config.hpp>

#if BEMAN_GATES_USE_MODULES() && !defined(BEMAN_GATES_INCLUDED_FROM_INTERFACE_UNIT)
import beman.gates;
import beman.execution;
#else
    #include <beman/gates/busy_error.hpp>
    #include <beman/gates/detail/task_base.hpp>
    #include <beman/gates/detail/task_queue.hpp>
    #include <beman/gates/detail/generic_sender.hpp>

    #include <beman/execution/execution.hpp>
#endif

namespace beman::gates::detail {

/// Operation state completing the execution of a task in the task queue described by `QueueTraits`.
template <typename QueueTraits, ::beman::execution::receiver Receiver>
struct complete_state {
    /// This is an operation state.
    using operation_state_concept = ::beman::execution::operation_state_tag;

    /// Constructs `*this` from `r` and `q`.
    complete_state(Receiver&& r, typename QueueTraits::queue_type* q)
        : receiver_{std::forward<Receiver>(r)}, queue_{q} {}

    /// Start the operation of completing executing in the task queue.
    void start() & noexcept {
        QueueTraits::on_scope_complete(queue_);
        ::beman::execution::set_value(std::move(receiver_));
    }

  private:
    /// The receiver that will be notified when the scope is exited.
    std::remove_cvref_t<Receiver> receiver_;
    /// The queue in which we need to complete executing.
    typename QueueTraits::queue_type* queue_;
};

/// Helper used to create a complete sender for the task queue described by `QueueTraits`.
template <typename QueueTraits>
struct complete_sender_for {
    /// The operation state type resulting from connecting to `Receiver`.
    template <::beman::execution::receiver Receiver>
    using state = complete_state<QueueTraits, Receiver>;

    /// The type of the complete sender for the task queue described by `QueueTraits`.
    using type = generic_sender<typename QueueTraits::queue_type*,
                                ::beman::execution::completion_signatures<::beman::execution::set_value_t()>,
                                state>;
};

/// The type of the complete sender for the task queue described by `QueueTraits`.
template <typename QueueTraits>
using complete_sender_t = typename complete_sender_for<QueueTraits>::type;

template <typename QueueTraits, ::beman::execution::receiver Receiver>
struct start_state : task_base {
    /// This is an operation state.
    using operation_state_concept = ::beman::execution::operation_state_tag;

    /// Constructs `*this` from `r` and `q`.
    start_state(Receiver&& r, typename QueueTraits::queue_type* q)
        : task_base{&do_start}, receiver_{std::forward<Receiver>(r)}, queue_{q} {}

    /// Start the operation of start executing in the task queue.
    void start() & noexcept {
        if constexpr (QueueTraits::speculative) {
            if (!QueueTraits::enqueue(queue_, this)) {
                ::beman::execution::set_error(std::move(receiver_), beman::gates::busy_error{});
            }
        } else {
            QueueTraits::enqueue(queue_, this);
        }
    }

  private:
    /// The receiver that will be notified when the scope is entered.
    std::remove_cvref_t<Receiver> receiver_;
    /// The queue in which we need to start executing.
    QueueTraits::queue_type* queue_;

    /// Called when the queue reaches the point where execution of our work can start.
    /// Completes the start sender by passing the complete sender to be executed when the actual work is complete.
    static void do_start(task_base* self) noexcept {
        auto* s = static_cast<start_state<QueueTraits, Receiver>*>(self);
        ::beman::execution::set_value(std::move(s->receiver_), complete_sender_t<QueueTraits>(s->queue_));
    }
};

template <typename QueueTraits>
struct start_sender_for {
    /// The operation state type resulting from connecting to `Receiver`.
    template <::beman::execution::receiver Receiver>
    using state = start_state<QueueTraits, Receiver>;

    /// The type of the start sender for the task queue described by `QueueTraits`.
    using type = generic_sender<
        typename QueueTraits::queue_type*,
        ::beman::execution::completion_signatures<::beman::execution::set_value_t(complete_sender_t<QueueTraits>)>,
        state>;
};

/// The type of the start sender for the task queue described by `QueueTraits`.
template <typename QueueTraits>
using start_sender_t = typename start_sender_for<QueueTraits>::type;

/// Returns an enter-scope sender that wraps `q`, using `QueueTraits` for interacting with it.
template <typename QueueTraits>
inline ::beman::execution::enter_scope_sender auto scope_over_queue(typename QueueTraits::queue_type* q) noexcept {
    using sender_t = start_sender_t<QueueTraits>;
    static_assert(::beman::execution::enter_scope_sender_in<sender_t, ::beman::execution::env<>>);
    return sender_t{q};
}

} // namespace beman::gates::detail

#endif
