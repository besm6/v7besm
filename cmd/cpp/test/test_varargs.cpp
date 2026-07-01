// C11 §6.10.3 / §6.10.3.1 — Variadic macros and __VA_ARGS__.
#include "test_support.h"

using Varargs = c11pp::PreprocessorTest;

TEST_F(Varargs, DISABLED_MultipleArguments) {
    EXPECT_TOKENS("#define P(...) __VA_ARGS__\nP(a,b,c)\n", "a,b,c");
}

TEST_F(Varargs, DISABLED_SingleArgument) {
    EXPECT_TOKENS("#define P(...) __VA_ARGS__\nP(x)\n", "x");
}

// A named parameter alongside the variadic part.
TEST_F(Varargs, DISABLED_NamedPlusVariadic) {
    EXPECT_TOKENS(
        "#define LOG(fmt, ...) printf(fmt, __VA_ARGS__)\n"
        "LOG(\"%d %d\", 1, 2)\n",
        "printf(\"%d %d\", 1, 2)");
}

// Commas inside the variadic argument list are preserved verbatim.
TEST_F(Varargs, DISABLED_CommasPreserved) {
    EXPECT_TOKENS(
        "#define FIRST(a, ...) a\n"
        "FIRST(1, 2, 3, 4)\n",
        "1");
}

// §6.10.3p5: __VA_ARGS__ may appear only in the replacement list of a macro
// that uses the ellipsis.
TEST_F(Varargs, DISABLED_VaArgsOutsideVariadicDiagnosed) {
    EXPECT_PP_DIAGNOSES("#define P(a) __VA_ARGS__\nP(1)\n");
}
