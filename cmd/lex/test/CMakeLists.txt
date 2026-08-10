#
# Unit tests for cmd/lex.  b6lex is a whole program with global tables and an
# error() that exits, so it is driven as a subprocess, as ../../yacc/test and
# ../../cpp/test do.
#
# B6LEXFORM_PATH points the tool at the skeleton in this tree; it is an installed
# file everywhere else.  B6_SCANNER_DIR reaches cmd/awk/awk.lx.l, the one scanner
# this machine has to compile.  HOST_CC compiles a generated scanner, which is
# how the C11 rewrite of ncform is asserted.
#
add_executable(lex_test lex_test.cpp)
target_link_libraries(lex_test PRIVATE GTest::gtest_main)
target_compile_definitions(lex_test PRIVATE
    B6LEX_COMMAND="$<TARGET_FILE:b6lex>"
    B6LEXFORM_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../ncform"
    B6_SCANNER_DIR="${CMAKE_SOURCE_DIR}/cmd"
    HOST_CC="${CMAKE_C_COMPILER}")
add_dependencies(lex_test b6lex)
gtest_discover_tests(lex_test)
add_dependencies(build_tests lex_test)
