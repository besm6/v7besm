// C11 §6.10.1 — Conditional inclusion.
#include "test_support.h"

using namespace c11pp;

TEST(Conditional, IfElifElse) {
    EXPECT_TOKENS(
        "#if 0\nA\n#elif 1\nB\n#else\nC\n#endif\n",
        "B");
}

TEST(Conditional, Nested) {
    EXPECT_TOKENS(
        "#if 1\n"
        "  #if 0\nX\n  #else\nY\n  #endif\n"
        "#endif\n",
        "Y");
}

TEST(Conditional, IfdefIfndef) {
    EXPECT_TOKENS(
        "#define M\n"
        "#ifdef M\nHAVE\n#endif\n"
        "#ifndef M\nMISSING\n#endif\n"
        "#ifndef N\nNO_N\n#endif\n",
        "HAVE\nNO_N");
}

// Both spellings of the `defined` operator, §6.10.1p1.
TEST(Conditional, DefinedBothForms) {
    EXPECT_TOKENS(
        "#define M 1\n"
        "#if defined M && defined(M) && !defined X\nOK\n#endif\n",
        "OK");
}

// §6.10.1p4: identifiers remaining after macro expansion are replaced by 0.
TEST(Conditional, UndefinedIdentifierIsZero) {
    EXPECT_TOKENS(
        "#if 2+2==4 && UNDEFINED_NAME==0\nOK\n#endif\n",
        "OK");
}

// A representative slice of the integer-constant-expression operator set.
TEST(Conditional, Operators) {
    EXPECT_TOKENS(
        "#if (1<<4) == 16 && (255 >> 4) == 15 && (6 & 3) == 2 && (1 | 4) == 5 "
        "&& (5 ^ 1) == 4 && (1 ? 2 : 3) == 2\nOK\n#endif\n",
        "OK");
}

// §6.10.1p4: character constants are permitted in the controlling expression.
TEST(Conditional, CharacterConstant) {
    EXPECT_TOKENS("#if 'A' == 65\nOK\n#endif\n", "OK");
}

// A skipped group is not evaluated, so the division by zero is inert.
TEST(Conditional, SkippedGroupNotEvaluated) {
    EXPECT_TOKENS("#if 0\n#if 1/0\n#endif\n#endif\nOK\n", "OK");
}

// §6.6: the controlling expression is an integer constant expression — a
// floating constant is a constraint violation.
TEST(Conditional, FloatingConstantDiagnosed) {
    EXPECT_PP_DIAGNOSES("#if 1.5 > 1\nX\n#endif\n");
}

// Assignment is not permitted in a constant expression.
TEST(Conditional, AssignmentDiagnosed) {
    EXPECT_PP_DIAGNOSES("#if (a = 1)\nX\n#endif\n");
}
