#
# Unit tests for the cmd/fsutil filesystem engine.
#
# NOT SERIALISED, and that is a constraint on what may be added here.  Every case
# in this directory writes image files into a shared working directory, and ctest
# runs each one as its own process, so a case that names a file must name it
# UNIQUELY -- per case, or per process where a fixture is built once per suite
# (damage_test.cpp, kernel_model_test.cpp).  The directory carried RUN_SERIAL until
# those two were the last using fixed names; it was the slowest thing in the suite.
#
add_executable(fsutil_test
    word_test.cpp
    superblock_test.cpp
    simh_test.cpp
    alloc_test.cpp
    inode_test.cpp
    dir_test.cpp
    create_test.cpp
    command_test.cpp
    kernel_model_test.cpp
    check_test.cpp
    damage_test.cpp
)
target_link_libraries(fsutil_test PRIVATE fsengine GTest::gtest_main)
gtest_discover_tests(fsutil_test)
add_dependencies(build_tests fsutil_test)
