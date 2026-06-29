#
# Unit tests for the cmd/disasm disassembler engine.
#
add_executable(disasm_test disasm_test.cpp)
target_link_libraries(disasm_test PRIVATE disassembler GTest::gtest_main)
gtest_discover_tests(disasm_test PROPERTIES RUN_SERIAL TRUE)
add_dependencies(build_tests disasm_test)
