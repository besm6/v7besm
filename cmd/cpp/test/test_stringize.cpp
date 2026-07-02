// C11 §6.10.3.2 — The # (stringize) operator.
#include "test_support.h"

using Stringize = PreprocessorTest;

// Leading/trailing whitespace deleted; interior whitespace collapsed to one
// space; the result is a string literal.
TEST_F(Stringize, Basic) {
    EXPECT_TOKENS("#define S(x) #x\nS(  hello   world  )\n", "\"hello world\"");
}

// §6.10.3.2p2: a \ is inserted before each " and \ that occurs within a
// character-constant or string-literal spelling in the argument.
TEST_F(Stringize, EscapesQuotesAndBackslashes) {
    EXPECT_TOKENS(
        "#define S(x) #x\n"
        "S(a \"b\" \\c)\n",
        "\"a \\\"b\\\" \\c\"");
}

// Two levels: the inner macro expands its argument, the outer stringizes the
// expansion — the classic XSTR idiom.
TEST_F(Stringize, ExpandsThroughIndirection) {
    EXPECT_TOKENS(
        "#define STR(x) #x\n"
        "#define XSTR(x) STR(x)\n"
        "#define VER 201112L\n"
        "XSTR(VER)\n",
        "\"201112L\"");
}

// A '#' in a function-like macro must be followed by a parameter (constraint).
TEST_F(Stringize, HashNotFollowedByParamDiagnosed) {
    EXPECT_PP_DIAGNOSES("#define S(x) # notaparam\nS(1)\n");
}
