// C11 §6.10.3 / §6.10.3.1 — Variadic macros and __VA_ARGS__.
#include "test_support.h"

using Varargs = PreprocessorTest;

TEST_F(Varargs, MultipleArguments) {
    EXPECT_TOKENS("#define P(...) __VA_ARGS__\nP(a,b,c)\n", "a,b,c");
}

TEST_F(Varargs, SingleArgument) {
    EXPECT_TOKENS("#define P(...) __VA_ARGS__\nP(x)\n", "x");
}

// A named parameter alongside the variadic part.
TEST_F(Varargs, NamedPlusVariadic) {
    EXPECT_TOKENS(
        "#define LOG(fmt, ...) printf(fmt, __VA_ARGS__)\n"
        "LOG(\"%d %d\", 1, 2)\n",
        "printf(\"%d %d\", 1, 2)");
}

// Commas inside the variadic argument list are preserved verbatim.
TEST_F(Varargs, CommasPreserved) {
    EXPECT_TOKENS(
        "#define FIRST(a, ...) a\n"
        "FIRST(1, 2, 3, 4)\n",
        "1");
}

// §6.10.3p5: __VA_ARGS__ may appear only in the replacement list of a macro
// that uses the ellipsis.
TEST_F(Varargs, VaArgsOutsideVariadicDiagnosed) {
    EXPECT_PP_DIAGNOSES("#define P(a) __VA_ARGS__\nP(1)\n");
}

// §6.10.3p5: __VA_ARGS__ in an object-like macro is a constraint violation too.
TEST_F(Varargs, VaArgsInObjectMacroDiagnosed) {
    EXPECT_PP_DIAGNOSES("#define X __VA_ARGS__\nX\n");
}

// GNU named varargs: an identifier before '...' binds all trailing arguments.
TEST_F(Varargs, NamedVarargs) {
    EXPECT_TOKENS("#define P(args...) f(args)\nP(1,2,3)\n", "f(1,2,3)");
}

TEST_F(Varargs, NamedVarargsEmpty) {
    EXPECT_TOKENS("#define P(args...) f(args)\nP()\n", "f()");
}

// GNU extension: __VA_ARGS__ also refers to a named variadic formal.
TEST_F(Varargs, NamedVarargsVaArgsAlias) {
    EXPECT_TOKENS("#define P(args...) f(__VA_ARGS__)\nP(1,2)\n", "f(1,2)");
}

// GNU ", ## __VA_ARGS__" comma elision: an empty variadic part drops the comma.
TEST_F(Varargs, CommaElisionEmpty) {
    EXPECT_TOKENS("#define P(fmt, ...) printf(fmt, ## __VA_ARGS__)\nP(\"x\")\n",
                  "printf(\"x\")");
}

TEST_F(Varargs, CommaElisionNonEmpty) {
    EXPECT_TOKENS("#define P(fmt, ...) printf(fmt, ## __VA_ARGS__)\nP(\"x\",1,2)\n",
                  "printf(\"x\",1,2)");
}
