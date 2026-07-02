// C11 §6.10.9 — The _Pragma unary operator.
#include "test_support.h"

using PragmaOperator = PreprocessorTest;

// _Pragma("STDC FP_CONTRACT ON") is destringized to a standard pragma and
// accepted (a conformant standard pragma, §6.10.6 / §7.6.1).
TEST_F(PragmaOperator, StandardPragmaAccepted) {
    EXPECT_PP_OK("_Pragma(\"STDC FP_CONTRACT ON\")\nOK\n");
}

// The surrounding tokens survive and the pragma is emitted as a #pragma line.
TEST_F(PragmaOperator, LeavesOtherTokens) {
    EXPECT_TOKENS(
        "before\n"
        "_Pragma(\"STDC FENV_ACCESS OFF\")\n"
        "after\n",
        "before\n#pragma STDC FENV_ACCESS OFF\nafter");
}

// §6.10.9: a _Pragma expression may itself be produced by macro expansion.
TEST_F(PragmaOperator, ProducedByMacro) {
    EXPECT_PP_OK(
        "#define DO_PRAGMA(x) _Pragma(#x)\n"
        "DO_PRAGMA(STDC CX_LIMITED_RANGE ON)\n"
        "OK\n");
}
