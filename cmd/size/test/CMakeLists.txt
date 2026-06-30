#
# Unit tests for the cmd/size engine.
#
add_executable(size_test size_test.cpp)
target_link_libraries(size_test PRIVATE size GTest::gtest_main)
gtest_discover_tests(size_test PROPERTIES RUN_SERIAL TRUE)
add_dependencies(build_tests size_test)
