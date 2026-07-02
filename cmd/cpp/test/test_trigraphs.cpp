// C11 §5.2.1.1 — Trigraph sequences (phase 1).  Valid in C11 (removed only in
// C23).  Trigraph replacement is flag-gated in several tools, so these tests
// explicitly request it and silence the associated warning.
#include "test_support.h"

using Trigraphs = PreprocessorTest;

static const std::vector<std::string> kTrigraphArgs = {"-trigraphs", "-w"};

// The trigraphs are written with \? escapes so that the host C++ compiler does
// not itself react to them; each \? yields a literal '?' at run time, so the
// snippet handed to the preprocessor-under-test is exactly "??...".

// ??= forms '#', so it can introduce a directive.
TEST_F(Trigraphs, HashIntroducesDirective) {
    EXPECT_TRUE(TokensAre(
        "\?\?=define X 1\n"
        "X\n",
        "1",
        kTrigraphArgs));
}

// The seven punctuation trigraphs each map to their single character.
TEST_F(Trigraphs, PunctuationMappings) {
    EXPECT_TRUE(TokensAre(
        "\?\?( \?\?) \?\?< \?\?> \?\?! \?\?' \?\?-\n",
        "[ ] { } | ^ ~",
        kTrigraphArgs));
}

// ??/ is a backslash, so ??/ at end of line performs line splicing.
TEST_F(Trigraphs, SlashActsAsLineSplice) {
    EXPECT_TRUE(TokensAre(
        "foo\?\?/\n"
        "bar\n",
        "foobar",
        kTrigraphArgs));
}

// Each conversion is reported (exit still 0), and -w silences the report.
TEST_F(Trigraphs, WarnsOnConversion) {
    Result warned = Preprocess("a\?\?!b\n", {"-trigraphs"});
    EXPECT_EQ(warned.exit_code, 0) << warned.err;
    EXPECT_NE(warned.err.find("trigraph"), std::string::npos)
        << "expected a trigraph warning on stderr, got: " << warned.err;

    Result quiet = Preprocess("a\?\?!b\n", {"-trigraphs", "-w"});
    EXPECT_EQ(quiet.exit_code, 0) << quiet.err;
    EXPECT_EQ(quiet.err.find("trigraph"), std::string::npos)
        << "-w should suppress the trigraph warning, got: " << quiet.err;
}

// An unrecognized command-line option is a fatal error (nonzero exit).
using CommandLine = PreprocessorTest;

TEST_F(CommandLine, UnknownOptionFails) {
    Result r = Preprocess("x\n", {"-zzz"});
    EXPECT_NE(r.exit_code, 0) << "expected nonzero exit for an unknown option";
}
