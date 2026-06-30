#
# Unit tests for the cmd/lorder shell script.
#
# lorder is a POSIX shell script, not a C program, so there is no engine library
# to link against.  The test drives the whole toolchain end to end: it assembles
# small objects with the real b6as, runs lorder over them with NM pointing at the
# real b6nm, and checks the dependency edge-list lorder prints for tsort.
#
add_executable(lorder_test lorder_test.cpp)
target_link_libraries(lorder_test PRIVATE GTest::gtest_main)
target_compile_definitions(lorder_test PRIVATE
    LORDER_SH="${CMAKE_CURRENT_SOURCE_DIR}/../lorder.sh"
    B6AS_EXE="$<TARGET_FILE:b6as>"
    B6NM_EXE="$<TARGET_FILE:b6nm>")
add_dependencies(lorder_test b6as b6nm) # the script chain shells out to both
gtest_discover_tests(lorder_test PROPERTIES RUN_SERIAL TRUE)
add_dependencies(build_tests lorder_test)
