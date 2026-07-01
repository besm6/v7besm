//
// A lightweight smoke-test suite for the cmd/cpp preprocessor.
//
// These are the quick sanity checks that predate the C11 conformance suite.
// They now share the same PreprocessorTest fixture and harness as the rest of
// the tests (see test_support.h / cpp_harness.*): each case is driven through
// Preprocess()/EXPECT_TOKENS rather than a hand-rolled shell invocation, so
// there is no bespoke file/spawn/compare code and no scratch files are left
// behind.
//
#include "test_support.h"

using Cpp = c11pp::PreprocessorTest;

// An object-like macro is expanded to its value.
TEST_F(Cpp, ObjectMacro) {
    EXPECT_TOKENS("#define N 42\nint x = N;\n", "int x = 42;");
}

// A function-like macro expands with its actual arguments substituted.
TEST_F(Cpp, FunctionMacro) {
    EXPECT_TOKENS("#define ADD(a,b) ((a)+(b))\nADD(1,2)\n", "((1)+(2))");
}

// Comments are stripped; a block comment collapses to a single space.
TEST_F(Cpp, StripsComments) {
    EXPECT_TOKENS("keep /* drop this */ more\n", "keep more");
}

// #ifdef selects the taken branch and drops the other; here FOO is undefined.
TEST_F(Cpp, IfdefUndefined) {
    EXPECT_TOKENS("#ifdef FOO\nyes\n#else\nno\n#endif\n", "no");
}

// #ifndef takes its branch when the macro is not defined.
TEST_F(Cpp, IfndefTaken) {
    EXPECT_TOKENS("#ifndef BAR\nnobar\n#endif\n", "nobar");
}

// A -D definition on the command line activates a matching #ifdef branch.
TEST_F(Cpp, CommandLineDefine) {
    EXPECT_TRUE(TokensAre("#ifdef FOO\nyes\n#else\nno\n#endif\n", "yes", {"-DFOO=1"}));
}

// #include pulls in a header found on the harness include path, and its macros
// apply.  The harness materialises aux files next to the source and puts that
// directory on the -I search path.
TEST_F(Cpp, IncludeWithSearchPath) {
    EXPECT_TRUE(TokensAre("#include \"hdr.h\"\nint y = FROMHDR;\n", "int y = 99;", {},
                          {{"hdr.h", "#define FROMHDR 99\n"}}));
}
