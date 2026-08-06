#
# Unit tests for the cmd/strip engine.
#
add_executable(strip_test strip_test.cpp)
target_link_libraries(strip_test PRIVATE strip GTest::gtest_main)
gtest_discover_tests(strip_test)
add_dependencies(build_tests strip_test)
