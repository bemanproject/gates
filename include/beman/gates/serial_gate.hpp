// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_GATES_SERIAL_GATE_HPP
#define BEMAN_GATES_SERIAL_GATE_HPP

#include <beman/gates/config.hpp>

#if BEMAN_GATES_USE_MODULES() && !defined(BEMAN_GATES_INCLUDED_FROM_INTERFACE_UNIT)

import beman.gates;

#else

    #include <beman/execution/execution.hpp>

namespace beman::gates {

/// A gate that allows maximum one work item to be executed at a time.
struct serial_gate {
    /// Returns an enter-scope sender that serializes protected work through `*this`.
    [[nodiscard]] inline ::beman::execution::enter_scope_sender auto acquire() noexcept;
};

inline ::beman::execution::enter_scope_sender auto serial_gate::acquire() noexcept {
    // TODO
    return ::beman::execution::just(::beman::execution::just());
}

} // namespace beman::gates

#endif // BEMAN_GATES_USE_MODULES() &&
       // !defined(BEMAN_GATES_INCLUDED_FROM_INTERFACE_UNIT)

#endif // BEMAN_GATES_SERIAL_GATE_HPP
