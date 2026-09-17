# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

find_package(GTest REQUIRED)
include(GoogleTest)

set(ALL_TESTS serial_gate shared_serial_gate serialized_access task_queue)
message("Tests to be built: ${ALL_TESTS}")

foreach(test ${ALL_TESTS})
    add_executable(beman.gates.tests.${test})
    target_sources(beman.gates.tests.${test} PRIVATE ${test}.test.cpp)
    target_link_libraries(beman.gates.tests.${test} PRIVATE beman::gates)
    target_link_libraries(
        beman.gates.tests.${test}
        PRIVATE beman::gates GTest::gtest_main
    )
    if(BEMAN_EXEMPLAR_USE_MODULES)
        set_target_properties(
            beman.gates.tests.${test}
            PROPERTIES CXX_MODULE_STD ON
        )
    endif()
    gtest_discover_tests(beman.gates.tests.${test} DISCOVERY_TIMEOUT 60)
endforeach()
