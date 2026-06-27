#
# Unit tests for the cmd/libaout (aout) header serializers.
#
add_executable(libaout_test fhdr_test.cpp ar_test.cpp half_test.cpp word_test.cpp sym_test.cpp)
target_link_libraries(libaout_test PRIVATE aout GTest::gtest_main)
gtest_discover_tests(libaout_test)
add_dependencies(build_tests libaout_test)
