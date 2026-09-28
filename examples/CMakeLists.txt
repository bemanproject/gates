# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

set(ALL_EXAMPLES
    serial_gate-cache
    serial_gate-work_outside_gate
    serial_gate-serializer_protocol
    serial_gate-save_doc
    serial_gate-try_save_doc
)
message("Examples to be built: ${ALL_EXAMPLES}")

foreach(example ${ALL_EXAMPLES})
    add_executable(beman.gates.examples.${example})
    target_sources(beman.gates.examples.${example} PRIVATE ${example}.cpp)
    set_target_properties(beman.gates.examples.${example} PROPERTIES OUTPUT_NAME ${example})
    target_link_libraries(beman.gates.examples.${example} PRIVATE beman::gates)
    if(BEMAN_GATES_USE_MODULES)
        set_target_properties(
            beman.gates.examples.${example}
            PROPERTIES CXX_MODULE_STD ON
        )
    endif()
endforeach()
