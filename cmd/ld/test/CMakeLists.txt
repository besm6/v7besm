#
# Unit test for the cmd/ld linker engine.
#
# The test assembles its input with the b6as binary, so it needs that tool's
# path (passed as B6AS_PATH) and a build dependency on it; the linking itself is
# done in-process through the `linker` library.
#
add_executable(ld_test ld_test.cpp)
target_link_libraries(ld_test PRIVATE linker GTest::gtest_main)
target_compile_definitions(ld_test PRIVATE
    B6AS_PATH="$<TARGET_FILE:b6as>"
    B6AR_PATH="$<TARGET_FILE:b6ar>")
add_dependencies(ld_test b6as b6ar)
gtest_discover_tests(ld_test PROPERTIES RUN_SERIAL TRUE)
add_dependencies(build_tests ld_test)
