#
# Unit tests for the BESM-6 simulator (b6sim).
#
add_executable(sim_test sim_test.cpp etc_test.cpp tty_test.cpp)
target_link_libraries(sim_test PRIVATE simulator GTest::gtest_main)
# etc_test.cpp compares b6sim's compiled-in copy of /etc against the tree it was transcribed
# from, so it needs to find the tree at run time.  A -D and not a -I: ../CMakeLists.txt keeps
# include/ off this library's search path deliberately, and a relative path would assume where
# the build directory sits.  See the header of etc_test.cpp.
target_compile_definitions(sim_test PRIVATE B6_TREE_ROOT="${CMAKE_SOURCE_DIR}")
gtest_discover_tests(sim_test)
add_dependencies(build_tests sim_test)
