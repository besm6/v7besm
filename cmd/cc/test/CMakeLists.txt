#
# End-to-end unit test for the b6cc driver: runs `b6cc -S` on a tiny C file and
# checks the generated assembler output.  Paths to the built b6cc and b6cpp are
# injected at compile time; the c-compiler passes (b6parse/b6lower/b6codegen)
# are found by the driver under ~/.local/bin | /usr/local/bin, and the test is
# skipped gracefully when they are not installed.
#
add_executable(cc_test cc_test.cpp)
target_link_libraries(cc_test PRIVATE GTest::gtest_main)
target_compile_definitions(cc_test PRIVATE
    B6CC_COMMAND="$<TARGET_FILE:b6cc>"
    B6CPP_PATH="$<TARGET_FILE:b6cpp>")
add_dependencies(cc_test b6cc b6cpp)
gtest_discover_tests(cc_test PROPERTIES RUN_SERIAL TRUE)
add_dependencies(build_tests cc_test)
