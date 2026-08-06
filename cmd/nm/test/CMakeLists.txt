#
# Unit tests for the cmd/nm engine.
#
add_executable(nm_test nm_test.cpp)
target_link_libraries(nm_test PRIVATE nm GTest::gtest_main)
gtest_discover_tests(nm_test)
add_dependencies(build_tests nm_test)
