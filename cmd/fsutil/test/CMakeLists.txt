#
# Unit tests for the cmd/fsutil filesystem engine.
#
# RUN_SERIAL because every one of these creates image files in the current
# directory under a fixed name.
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
)
target_link_libraries(fsutil_test PRIVATE fsengine GTest::gtest_main)
gtest_discover_tests(fsutil_test PROPERTIES RUN_SERIAL TRUE)
add_dependencies(build_tests fsutil_test)
