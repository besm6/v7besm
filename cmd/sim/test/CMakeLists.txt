#
# Unit tests for the BESM-6 simulator (b6sim).
#
add_executable(sim_test sim_test.cpp)
target_link_libraries(sim_test PRIVATE simulator GTest::gtest_main)
gtest_discover_tests(sim_test PROPERTIES RUN_SERIAL TRUE)
add_dependencies(build_tests sim_test)
