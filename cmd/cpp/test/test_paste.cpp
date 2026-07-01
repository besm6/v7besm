// C11 §6.10.3.3 — The ## (token paste) operator.
#include "test_support.h"

using Paste = c11pp::PreprocessorTest;

TEST_F(Paste, DISABLED_IdentifierPaste) {
    EXPECT_TOKENS("#define C(a,b) a##b\nC(foo,bar)\n", "foobar");
}

// Pasting two pp-numbers yields a single pp-number token.
TEST_F(Paste, DISABLED_NumberPaste) {
    EXPECT_TOKENS("#define C(a,b) a##b\nC(12,34)\n", "1234");
}

// §6.10.3.3p2: an empty operand acts as a placemarker; the other operand
// survives unchanged.
TEST_F(Paste, DISABLED_EmptyLeftOperand) {
    EXPECT_TOKENS("#define C(a,b) a##b\n[C(,tail)]\n", "[tail]");
}

TEST_F(Paste, DISABLED_EmptyRightOperand) {
    EXPECT_TOKENS("#define C(a,b) a##b\n[C(head,)]\n", "[head]");
}

// Paste combined with rescanning: build an identifier then expand it.
TEST_F(Paste, DISABLED_ResultIsRescanned) {
    EXPECT_TOKENS(
        "#define VALUE 99\n"
        "#define C(a,b) a##b\n"
        "C(VAL,UE)\n",
        "99");
}

// §6.10.3.3p1: ## shall not occur at the start of a replacement list.
TEST_F(Paste, DISABLED_AtStartDiagnosed) {
    EXPECT_PP_DIAGNOSES("#define C(a) ##a\nC(1)\n");
}

// §6.10.3.3p1: ## shall not occur at the end of a replacement list.
TEST_F(Paste, DISABLED_AtEndDiagnosed) {
    EXPECT_PP_DIAGNOSES("#define C(a) a##\nC(1)\n");
}
