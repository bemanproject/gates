# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

find_package(GTest REQUIRED)

add_executable(beman.gates.tests.serial_gate)
target_sources(beman.gates.tests.serial_gate PRIVATE serial_gate.test.cpp)
target_link_libraries(
    beman.gates.tests.serial_gate
    PRIVATE beman::gates GTest::gtest_main
)
if(BEMAN_EXEMPLAR_USE_MODULES)
    set_target_properties(
        beman.gates.tests.serial_gate
        PROPERTIES CXX_MODULE_STD ON
    )
endif()

include(GoogleTest)
gtest_discover_tests(beman.gates.tests.serial_gate DISCOVERY_TIMEOUT 60)

add_executable(beman.gates.tests.task_queue)
target_sources(beman.gates.tests.task_queue PRIVATE task_queue.test.cpp)
target_link_libraries(
    beman.gates.tests.task_queue
    PRIVATE beman::gates GTest::gtest_main
)
if(BEMAN_EXEMPLAR_USE_MODULES)
    set_target_properties(
        beman.gates.tests.task_queue
        PROPERTIES CXX_MODULE_STD ON
    )
endif()

gtest_discover_tests(beman.gates.tests.task_queue DISCOVERY_TIMEOUT 60)
