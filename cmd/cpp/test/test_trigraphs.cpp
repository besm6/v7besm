// C11 §5.2.1.1 — Trigraph sequences (phase 1).  Valid in C11 (removed only in
// C23).  Trigraph replacement is flag-gated in several tools, so these tests
// explicitly request it and silence the associated warning.
#include "test_support.h"

using Trigraphs = c11pp::PreprocessorTest;

namespace {
const std::vector<std::string> kTrigraphArgs = {"-trigraphs", "-w"};
}

// The trigraphs are written with \? escapes so that the host C++ compiler does
// not itself react to them; each \? yields a literal '?' at run time, so the
// snippet handed to the preprocessor-under-test is exactly "??...".

// ??= forms '#', so it can introduce a directive.
TEST_F(Trigraphs, DISABLED_HashIntroducesDirective) {
    EXPECT_TRUE(TokensAre(
        "\?\?=define X 1\n"
        "X\n",
        "1",
        kTrigraphArgs));
}

// The seven punctuation trigraphs each map to their single character.
TEST_F(Trigraphs, DISABLED_PunctuationMappings) {
    EXPECT_TRUE(TokensAre(
        "\?\?( \?\?) \?\?< \?\?> \?\?! \?\?' \?\?-\n",
        "[ ] { } | ^ ~",
        kTrigraphArgs));
}

// ??/ is a backslash, so ??/ at end of line performs line splicing.
TEST_F(Trigraphs, DISABLED_SlashActsAsLineSplice) {
    EXPECT_TRUE(TokensAre(
        "foo\?\?/\n"
        "bar\n",
        "foobar",
        kTrigraphArgs));
}
