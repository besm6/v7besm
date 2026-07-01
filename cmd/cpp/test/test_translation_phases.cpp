// C11 §5.1.1.2 — Translation phases (as observable through the preprocessor).
// Phase 2: backslash-newline splicing.  Phase 3: comments become one space.
#include "test_support.h"

using TranslationPhases = c11pp::PreprocessorTest;

// Phase 2: a backslash-newline in a macro replacement list is spliced away.
TEST_F(TranslationPhases, LineSpliceInReplacementList) {
    EXPECT_TOKENS(
        "#define A 1+\\\n2\n"
        "A\n",
        "1+2");
}

// Phase 2: splicing occurs before tokenization, so it can join a single token.
TEST_F(TranslationPhases, LineSpliceJoinsToken) {
    EXPECT_TOKENS(
        "#define CAT foo\\\nbar\n"
        "CAT\n",
        "foobar");
}

// Phase 3: a block comment is replaced by a single space (it does not vanish).
TEST_F(TranslationPhases, DISABLED_BlockCommentBecomesSpace) {
    EXPECT_TOKENS("a/**/b\n", "a b");
}

// Phase 3: a // comment runs to end of line and is removed.
TEST_F(TranslationPhases, DISABLED_LineCommentRemoved) {
    EXPECT_TOKENS(
        "a//trailing\n"
        "b\n",
        "a\nb");
}

// Phase 3: a block comment may span multiple physical lines.
TEST_F(TranslationPhases, BlockCommentSpansLines) {
    EXPECT_TOKENS(
        "x/* one\n"
        "two */y\n",
        "x y");
}

// A comment that is never closed cannot be tokenized: a diagnostic is required.
TEST_F(TranslationPhases, DISABLED_UnterminatedCommentDiagnosed) {
    EXPECT_PP_DIAGNOSES("a /* never closed\n");
}
