// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_GATES_DETAIL_SCOPE_OVER_QUEUE_HPP
#define BEMAN_GATES_DETAIL_SCOPE_OVER_QUEUE_HPP

#include <beman/gates/config.hpp>

#if BEMAN_GATES_USE_MODULES() && !defined(BEMAN_GATES_INCLUDED_FROM_INTERFACE_UNIT)
import beman.gates;
import beman.execution;
#else
    #include <beman/gates/detail/task_base.hpp>
    #include <beman/gates/detail/task_queue.hpp>

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

struct complete_sender {
    /// This is a sender.
    using sender_concept = ::beman::execution::sender_tag;
    /// The completion signatures of this sender.
    using completion_signatures = ::beman::execution::completion_signatures<::beman::execution::set_value_t()>;

    /// Constructs `*this` from `q`.
    complete_sender(task_queue* q) : queue_{q} {}

    /// Returns the async operation corresponding to `*this` connected to `receiver`.
    template <::beman::execution::receiver Receiver>
    complete_state<Receiver> connect(Receiver&& receiver) const noexcept {
        return {std::forward<Receiver>(receiver), queue_};
    }

    /// Returns the completion signatures of `*this`.
    template <typename, typename...>
    static consteval completion_signatures get_completion_signatures() noexcept {
        return {};
    }

  private:
    /// The queue in which we need to start executing.
    task_queue* queue_;
};

/// Operation state corresponding to the async operation of start executing in the task queue.
template <::beman::execution::receiver Receiver>
struct start_state : task_base {
    /// This is an operation state.
    using operation_state_concept = ::beman::execution::operation_state_tag;

    /// Constructs `*this` from `r` and `q`.
    start_state(Receiver&& r, task_queue* q) : task_base{&complete}, receiver_{std::forward<Receiver>(r)}, queue_{q} {}

    /// Start the operation of start executing in the task queue.
    void start() & noexcept { queue_->enqueue(this); }

  private:
    /// The receiver that will be notified when the scope is entered.
    std::remove_cvref_t<Receiver> receiver_;
    /// The queue in which we need to start executing.
    task_queue* queue_;

    /// Called when we are start executing in the queue, to complete the operation and notify the receiver.
    static void complete(task_base* self) noexcept {
        auto* s = static_cast<start_state*>(self);
        ::beman::execution::set_value(std::move(s->receiver_), complete_sender{s->queue_});
    }
};

/// Sender that starts executing in the task queue.
struct start_sender {
    /// This is a sender.
    using sender_concept = ::beman::execution::sender_tag;
    /// The completion signatures of this sender.
    using completion_signatures =
        ::beman::execution::completion_signatures<::beman::execution::set_value_t(complete_sender)>;

    /// Constructs `*this` from `q`.
    start_sender(task_queue* q) : queue_{q} {}

    /// Returns the async operation corresponding to `*this` connected to `receiver`.
    template <::beman::execution::receiver Receiver>
    start_state<Receiver> connect(Receiver&& receiver) const noexcept {
        return {std::forward<Receiver>(receiver), queue_};
    }

    /// Returns the completion signatures of `*this`.
    template <typename, typename...>
    static consteval completion_signatures get_completion_signatures() noexcept {
        return {};
    }

  private:
    /// The queue in which we need to start executing.
    task_queue* queue_;
};

/// Returns an enter-scope sender that wraps `q`.
///
/// This will ensure that `q->enqueue()` is called when the scope is entered, and `q->on_task_complete()` is called
/// when the scope is exited.
inline ::beman::execution::enter_scope_sender auto scope_over_queue(task_queue* q) noexcept {
    static_assert(::beman::execution::exit_scope_sender_in<detail::complete_sender, ::beman::execution::env<>>);
    static_assert(::beman::execution::enter_scope_sender_in<detail::start_sender, ::beman::execution::env<>>);
    return detail::start_sender{q};
}

} // namespace beman::gates::detail

#endif
