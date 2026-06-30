#
# Unit tests for the cmd/ar archiver engine.
#
add_executable(ar_test ar_test.cpp)
target_link_libraries(ar_test PRIVATE archiver GTest::gtest_main)
gtest_discover_tests(ar_test PROPERTIES RUN_SERIAL TRUE)
add_dependencies(build_tests ar_test)
