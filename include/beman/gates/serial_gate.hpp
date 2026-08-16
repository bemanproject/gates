// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_GATES_SERIAL_GATE_HPP
#define BEMAN_GATES_SERIAL_GATE_HPP

#include <beman/gates/config.hpp>

#if BEMAN_GATES_USE_MODULES() && !defined(BEMAN_GATES_INCLUDED_FROM_INTERFACE_UNIT)
import beman.gates;
import beman.execution;
#else
    #include <beman/gates/detail/task_queue.hpp>
    #include <beman/gates/detail/scope_over_queue.hpp>

    #include <beman/execution/execution.hpp>
#endif

namespace beman::gates {

/// A gate that allows maximum one work item to be executed at a time.
struct serial_gate {
    serial_gate()  = default;
    ~serial_gate() = default;

    serial_gate(const serial_gate&) = delete;
    serial_gate(serial_gate&&)      = delete;

    /// Returns an enter-scope sender that serializes protected work through `*this`.
    [[nodiscard]] inline ::beman::execution::enter_scope_sender auto acquire() noexcept {
        return detail::scope_over_queue(&queue_);
    }

  private:
    /// The queue that serializes work through this gate.
    detail::task_queue queue_;
};

} // namespace beman::gates

#endif
