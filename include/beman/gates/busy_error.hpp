// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_GATES_BUSY_ERROR_HPP
#define BEMAN_GATES_BUSY_ERROR_HPP

#include <beman/gates/config.hpp>

#ifdef BEMAN_HAS_IMPORT_STD
import std;
#else
    #include <exception>
#endif

namespace beman::gates {

/// Indicates that a non-blocking `try_` operation could not enter because the gate was busy.
struct busy_error : std::exception {};

} // namespace beman::gates

#endif
