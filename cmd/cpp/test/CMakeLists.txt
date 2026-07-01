#
# End-to-end unit tests for the C preprocessor: they drive the built b6cpp
# binary over real input and check the preprocessed output.  The binary path is
# injected at compile time via a generator expression.
#
add_executable(cpp_test cpp_test.cpp)
target_link_libraries(cpp_test PRIVATE GTest::gtest_main)
target_compile_definitions(cpp_test PRIVATE B6CPP_PATH="$<TARGET_FILE:b6cpp>")
add_dependencies(cpp_test b6cpp)
gtest_discover_tests(cpp_test PROPERTIES RUN_SERIAL TRUE)
add_dependencies(build_tests cpp_test)
