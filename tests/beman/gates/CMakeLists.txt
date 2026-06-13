# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

find_package(GTest REQUIRED)

add_executable(beman.gates.tests.todo)
target_sources(beman.gates.tests.todo PRIVATE todo.test.cpp)
target_link_libraries(
    beman.gates.tests.todo
    PRIVATE beman::gates GTest::gtest_main
)
if(BEMAN_EXEMPLAR_USE_MODULES)
    set_target_properties(beman.gates.tests.todo PROPERTIES CXX_MODULE_STD ON)
endif()

include(GoogleTest)
gtest_discover_tests(beman.gates.tests.todo DISCOVERY_TIMEOUT 60)
