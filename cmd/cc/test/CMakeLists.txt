#
# End-to-end unit tests for the b6cc driver: runs `b6cc -S` on a tiny C file and
# checks the generated assembler output, the .S (preprocessed assembly) path
# through b6cpp and b6as, and a real link down to an executable.  Paths to the
# built b6cc, b6cpp, b6as and b6ld are injected at compile time; the c-compiler
# passes (b6parse/b6lower/b6codegen) are found by the driver under ~/.local/bin |
# /usr/local/bin, and tests that need them are skipped gracefully when they are
# not installed.  The link test additionally needs an installed libc.a/crt0.o
# (from `make install'), and skips without it.
#
add_executable(cc_test cc_test.cpp)
target_link_libraries(cc_test PRIVATE GTest::gtest_main)
target_compile_definitions(cc_test PRIVATE
    B6CC_COMMAND="$<TARGET_FILE:b6cc>"
    B6CPP_PATH="$<TARGET_FILE:b6cpp>"
    B6AS_PATH="$<TARGET_FILE:b6as>"
    B6LD_PATH="$<TARGET_FILE:b6ld>")
add_dependencies(cc_test b6cc b6cpp b6as b6ld)
gtest_discover_tests(cc_test PROPERTIES RUN_SERIAL TRUE)
add_dependencies(build_tests cc_test)
