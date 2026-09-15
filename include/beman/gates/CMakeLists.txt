# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

if(BEMAN_GATES_USE_MODULES)
    target_sources(
        beman.gates
        PUBLIC
            FILE_SET CXX_MODULES FILES gates.cppm
            FILE_SET HEADERS
                FILES
                    config.hpp
                    gates.hpp
                    busy_error.hpp
                    serial_gate.hpp
                    serialized_access.hpp
                    detail/generic_sender.hpp
                    detail/scope_over_queue.hpp
                    detail/task_base.hpp
                    detail/task_queue.hpp
                    "${PROJECT_BINARY_DIR}/include/beman/gates/config_generated.hpp"
    )
else()
    target_sources(
        beman.gates
        PUBLIC
            FILE_SET HEADERS
                FILES
                    config.hpp
                    gates.hpp
                    busy_error.hpp
                    serial_gate.hpp
                    serialized_access.hpp
                    detail/generic_sender.hpp
                    detail/scope_over_queue.hpp
                    detail/task_base.hpp
                    detail/task_queue.hpp
                    "${PROJECT_BINARY_DIR}/include/beman/gates/config_generated.hpp"
    )
endif()
