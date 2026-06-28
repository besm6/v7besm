#
# Unit tests for the cmd/as assembler.
#
add_executable(as_test as_test.cpp)
target_link_libraries(as_test PRIVATE assembler GTest::gtest_main)
gtest_discover_tests(as_test PROPERTIES RUN_SERIAL TRUE)
add_dependencies(build_tests as_test)
