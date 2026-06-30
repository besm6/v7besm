#
# Unit tests for the cmd/ranlib engine.
#
add_executable(ranlib_test ranlib_test.cpp)
target_link_libraries(ranlib_test PRIVATE ranlib GTest::gtest_main)
gtest_discover_tests(ranlib_test PROPERTIES RUN_SERIAL TRUE)
add_dependencies(build_tests ranlib_test)
