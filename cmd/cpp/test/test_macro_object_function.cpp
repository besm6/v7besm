// C11 §6.10.3, .3.1, .3.4, .3.5 — Macro replacement, argument substitution,
// rescanning, and the scope of definitions.
#include "test_support.h"

using Macro = PreprocessorTest;

TEST_F(Macro, ObjectLike) {
    EXPECT_TOKENS("#define N 42\nN\n", "42");
}

TEST_F(Macro, FunctionLike) {
    EXPECT_TOKENS("#define ADD(a,b) a+b\nADD(2,3)\n", "2+3");
}

TEST_F(Macro, EmptyReplacementList) {
    EXPECT_TOKENS("#define NOTHING\n[NOTHING]\n", "[]");
}

// §6.10.3.1: arguments are fully macro-expanded before substitution.
TEST_F(Macro, ArgumentsPrescanned) {
    EXPECT_TOKENS(
        "#define ONE 1\n"
        "#define ADD(a,b) a+b\n"
        "ADD(ONE,ONE)\n",
        "1+1");
}

// §6.10.3.4p2: a macro is not re-expanded during rescan of its own expansion.
// f(1)(2) -> "1 f" then rescanning does not re-invoke f -> "1 f(2)".
TEST_F(Macro, NoRescanRecursion) {
    EXPECT_TOKENS("#define f(x) x f\nf(1)(2)\n", "1 f(2)");
}

// A self-referential object-like macro expands to itself once (blue paint).
TEST_F(Macro, SelfReference) {
    EXPECT_TOKENS("#define X X\nX\n", "X");
}

// A function-like invocation may span several physical lines.
TEST_F(Macro, InvocationSpansLines) {
    EXPECT_TOKENS(
        "#define ADD(a,b) a+b\n"
        "ADD(\n  10,\n  20\n)\n",
        "10+20");
}

// A bare mention of a function-like macro name (no '(') is not an invocation.
TEST_F(Macro, NameWithoutParensNotInvoked) {
    EXPECT_TOKENS("#define F(x) x\nF\n", "F");
}

TEST_F(Macro, Undef) {
    EXPECT_TOKENS("#define X 1\n#undef X\nX\n", "X");
}

// §6.10.3p2: an identical redefinition is permitted (no diagnostic).
TEST_F(Macro, IdenticalRedefinitionAllowed) {
    EXPECT_TOKENS("#define M 1\n#define M 1\nM\n", "1");
}

// §6.10.3p2: an incompatible redefinition requires a diagnostic.
TEST_F(Macro, IncompatibleRedefinitionDiagnosed) {
    EXPECT_PP_DIAGNOSES("#define M 1\n#define M 2\nM\n");
}

// §6.10.3p4: too few arguments to a function-like macro.
TEST_F(Macro, TooFewArgumentsDiagnosed) {
    EXPECT_PP_DIAGNOSES("#define F(a,b) a b\nF(1)\n");
}

// §6.10.3p4: too many arguments to a function-like macro.
TEST_F(Macro, TooManyArgumentsDiagnosed) {
    EXPECT_PP_DIAGNOSES("#define F(a) a\nF(1,2)\n");
}
