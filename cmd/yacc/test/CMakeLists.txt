#
# Unit tests for cmd/yacc.  b6yacc is a whole program with global tables and an
# error() that exits, so it is driven as a subprocess, as cmd/cpp/test does.
#
# B6YACCPAR_PATH points the tool at the skeleton in this tree; it is an
# installed file everywhere else.  B6_GRAMMAR_DIR reaches the .y files sitting
# under cmd/ that tasks C11-C17 will feed it.  HOST_CC compiles a generated
# parser, which is how the C11 rewrite of yaccpar.c is asserted.
#
add_executable(yacc_test yacc_test.cpp)
target_link_libraries(yacc_test PRIVATE GTest::gtest_main)
target_compile_definitions(yacc_test PRIVATE
    B6YACC_COMMAND="$<TARGET_FILE:b6yacc>"
    B6YACCPAR_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../yaccpar.c"
    B6_GRAMMAR_DIR="${CMAKE_SOURCE_DIR}/cmd"
    HOST_CC="${CMAKE_C_COMPILER}")
add_dependencies(yacc_test b6yacc)
gtest_discover_tests(yacc_test)
add_dependencies(build_tests yacc_test)
