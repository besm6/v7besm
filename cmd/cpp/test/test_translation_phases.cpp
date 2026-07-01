// C11 §5.1.1.2 — Translation phases (as observable through the preprocessor).
// Phase 2: backslash-newline splicing.  Phase 3: comments become one space.
#include "test_support.h"

using namespace c11pp;

// Phase 2: a backslash-newline in a macro replacement list is spliced away.
TEST(TranslationPhases, LineSpliceInReplacementList) {
    EXPECT_TOKENS(
        "#define A 1+\\\n2\n"
        "A\n",
        "1+2");
}

// Phase 2: splicing occurs before tokenization, so it can join a single token.
TEST(TranslationPhases, LineSpliceJoinsToken) {
    EXPECT_TOKENS(
        "#define CAT foo\\\nbar\n"
        "CAT\n",
        "foobar");
}

// Phase 3: a block comment is replaced by a single space (it does not vanish).
TEST(TranslationPhases, BlockCommentBecomesSpace) {
    EXPECT_TOKENS("a/**/b\n", "a b");
}

// Phase 3: a // comment runs to end of line and is removed.
TEST(TranslationPhases, LineCommentRemoved) {
    EXPECT_TOKENS(
        "a//trailing\n"
        "b\n",
        "a\nb");
}

// Phase 3: a block comment may span multiple physical lines.
TEST(TranslationPhases, BlockCommentSpansLines) {
    EXPECT_TOKENS(
        "x/* one\n"
        "two */y\n",
        "x y");
}

// A comment that is never closed cannot be tokenized: a diagnostic is required.
TEST(TranslationPhases, UnterminatedCommentDiagnosed) {
    EXPECT_PP_DIAGNOSES("a /* never closed\n");
}
