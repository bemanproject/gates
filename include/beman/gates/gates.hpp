// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_GATES_GATES_HPP
#define BEMAN_GATES_GATES_HPP

#include <beman/gates/config.hpp>

#if BEMAN_GATES_USE_MODULES() && !defined(BEMAN_GATES_INCLUDED_FROM_INTERFACE_UNIT)

import beman.gates;

#else

    #include <beman/gates/serial_gate.hpp>

#endif // BEMAN_GATES_USE_MODULES() &&
       // !defined(BEMAN_GATES_INCLUDED_FROM_INTERFACE_UNIT)

#endif // BEMAN_GATES_GATES_HPP
