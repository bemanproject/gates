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

/// Operation state corresponding to the async operation of completing executing in the task queue.
template <::beman::execution::receiver Receiver>
struct complete_state {
    /// This is an operation state.
    using operation_state_concept = ::beman::execution::operation_state_tag;

    /// Constructs `*this` from `r` and `q`.
    complete_state(Receiver&& r, task_queue* q) : receiver_{std::forward<Receiver>(r)}, queue_{q} {}

    /// Start the operation of completing executing in the task queue.
    void start() & noexcept {
        queue_->on_task_complete();
        ::beman::execution::set_value(std::move(receiver_));
    }

  private:
    /// The receiver that will be notified when the scope is exited.
    std::remove_cvref_t<Receiver> receiver_;
    /// The queue in which we need to complete executing.
    task_queue* queue_;
};

/// Sender that describes the operation of leaving the task queue.
using complete_sender = generic_sender<task_queue*,
                                       ::beman::execution::completion_signatures<::beman::execution::set_value_t()>,
                                       complete_state>;

/// Operation state corresponding to the async operation of start executing in the task queue, in speculative mode or
/// not.
template <bool Speculative, ::beman::execution::receiver Receiver>
struct start_state_base : task_base {
    /// This is an operation state.
    using operation_state_concept = ::beman::execution::operation_state_tag;

    /// Constructs `*this` from `r` and `q`.
    start_state_base(Receiver&& r, task_queue* q)
        : task_base{&do_start}, receiver_{std::forward<Receiver>(r)}, queue_{q} {}

    /// Start the operation of start executing in the task queue.
    void start() & noexcept {
        if constexpr (Speculative) {
            if (!queue_->try_enqueue(this)) {
                ::beman::execution::set_error(std::move(receiver_), beman::gates::busy_error{});
            }
        } else {
            queue_->enqueue(this);
        }
    }

  private:
    /// The receiver that will be notified when the scope is entered.
    std::remove_cvref_t<Receiver> receiver_;
    /// The queue in which we need to start executing.
    task_queue* queue_;

    /// Called when the queue reaches the point where execution of our work can start.
    /// Completes the start sender by passing the complete sender to be executed when the actual work is complete.
    static void do_start(task_base* self) noexcept {
        auto* s = static_cast<start_state_base*>(self);
        ::beman::execution::set_value(std::move(s->receiver_), complete_sender{s->queue_});
    }
};

/// Operation state corresponding to the async operation of start executing in the task queue.
template <::beman::execution::receiver Receiver>
using start_state = start_state_base<false, Receiver>;

/// Operation state corresponding to the async operation of start executing in the task queue.
template <::beman::execution::receiver Receiver>
using try_start_state = start_state_base<true, Receiver>;

/// Sender that describes the operation of entering the task queue.
using start_sender =
    generic_sender<task_queue*,
                   ::beman::execution::completion_signatures<::beman::execution::set_value_t(complete_sender)>,
                   start_state>;

/// Sender that describes the operation of entering the task queue if the task queue is not busy.
using try_start_sender = generic_sender<
    task_queue*,
    ::beman::execution::completion_signatures<::beman::execution::set_value_t(complete_sender),
                                              ::beman::execution::set_error_t(beman::gates::busy_error)>,
    try_start_state>;

/// Returns an enter-scope sender that wraps `q`.
///
/// This will ensure that `q->enqueue()` is called when the scope is entered, and `q->on_task_complete()` is called
/// when the scope is exited.
inline ::beman::execution::enter_scope_sender auto scope_over_queue(task_queue* q) noexcept {
    static_assert(::beman::execution::exit_scope_sender_in<detail::complete_sender, ::beman::execution::env<>>);
    static_assert(::beman::execution::enter_scope_sender_in<detail::start_sender, ::beman::execution::env<>>);
    return detail::start_sender{q};
}

/// Returns an enter-scope sender that tries to reserve `q` without waiting.
///
/// If the reservation succeeds, the sender completes with an exit-scope sender that releases the reservation when
/// the scope is exited. If `q` is busy, it completes with `set_error(beman::gates::busy_error{})`.
inline ::beman::execution::enter_scope_sender auto try_scope_over_queue(task_queue* q) noexcept {
    static_assert(::beman::execution::exit_scope_sender_in<detail::complete_sender, ::beman::execution::env<>>);
    static_assert(::beman::execution::enter_scope_sender_in<detail::try_start_sender, ::beman::execution::env<>>);
    return detail::try_start_sender{q};
}

} // namespace beman::gates::detail

#endif
