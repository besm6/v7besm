#
# End-to-end unit tests for the b6cc driver: runs `b6cc -S` on a tiny C file and
# checks the generated assembler output, plus the .S (preprocessed assembly)
# path through b6cpp and b6as.  Paths to the built b6cc, b6cpp and b6as are
# injected at compile time; the c-compiler passes (b6parse/b6lower/b6codegen)
# are found by the driver under ~/.local/bin | /usr/local/bin, and tests that
# need them are skipped gracefully when they are not installed.
#
add_executable(cc_test cc_test.cpp)
target_link_libraries(cc_test PRIVATE GTest::gtest_main)
target_compile_definitions(cc_test PRIVATE
    B6CC_COMMAND="$<TARGET_FILE:b6cc>"
    B6CPP_PATH="$<TARGET_FILE:b6cpp>"
    B6AS_PATH="$<TARGET_FILE:b6as>")
add_dependencies(cc_test b6cc b6cpp b6as)
gtest_discover_tests(cc_test PROPERTIES RUN_SERIAL TRUE)
add_dependencies(build_tests cc_test)
