// C11 §6.10.4 (#line), §6.10.5 (#error), §6.10.6 (#pragma), §6.10.7 (null).
#include "test_support.h"

using LineControl    = PreprocessorTest;
using ErrorDirective = PreprocessorTest;
using Pragma         = PreprocessorTest;
using NullDirective  = PreprocessorTest;

// §6.10.4: #line sets the presumed line number and file name.
TEST_F(LineControl, SetsLineAndFile) {
    EXPECT_TOKENS(
        "#line 100 \"foo.c\"\n"
        "__LINE__ __FILE__\n",
        "100 \"foo.c\"");
}

TEST_F(LineControl, SetsLineOnly) {
    EXPECT_TOKENS(
        "#line 500\n"
        "__LINE__\n",
        "500");
}

// §6.10.4p5: the digit sequence / file name may be produced by macro expansion.
TEST_F(LineControl, MacroExpandedOperands) {
    EXPECT_TOKENS(
        "#define WHERE 250\n"
        "#line WHERE\n"
        "__LINE__\n",
        "250");
}

// §6.10.4: the first operand must be a digit sequence; a non-numeric #line is a
// constraint violation and must prevent successful translation.
TEST_F(LineControl, NonDigitOperandDiagnosed) {
    EXPECT_PP_DIAGNOSES("#line notanumber\n");
}

// The directive on the NEXT line is still a directive.  #line used to leave the
// scan pointer past its own newline, so the loop's fast scan for "\n#" walked
// over whatever came next and took it for text -- silently, and whatever it was.
// README.md, "A directive after #line".
TEST_F(LineControl, NextLineDirectiveNotSwallowed) {
    EXPECT_TOKENS(
        "#line 11\n"
        "#define AA 7\n"
        "AA\n",
        "7");
}

// The shape that found it: b6yacc writes `# line N "file"' straight above the
// %{ ... %} block it copies out of a grammar, and cmd/lex/parser.y opens that
// block with an #include.
TEST_F(LineControl, NextLineIncludeNotSwallowed) {
    EXPECT_TRUE(TokensAre(
        "# line 11 \"gen.y\"\n"
        "#include \"defs.h\"\n"
        "TAG\n",
        "ok",
        {},
        {{"defs.h", "#define TAG ok\n"}}));
}

// §6.10.5: a #error not skipped by conditional inclusion prevents successful
// translation.
TEST_F(ErrorDirective, StopsTranslation) {
    EXPECT_PP_DIAGNOSES("code\n#error deliberate failure\nmore\n");
}

// A #error inside a skipped group is inert.
TEST_F(ErrorDirective, SkippedErrorIsInert) {
    EXPECT_TOKENS("#if 0\n#error not reached\n#endif\nOK\n", "OK");
}

// §6.10.6: an unrecognized #pragma is ignored (implementation-defined, but must
// not fail translation).
TEST_F(Pragma, UnknownPragmaAccepted) {
    EXPECT_PP_OK("#pragma this is not a real pragma\nOK\n");
}

// §6.10.7: a # directive with nothing after it has no effect.
TEST_F(NullDirective, HasNoEffect) {
    EXPECT_TOKENS("#\nOK\n#   \n", "OK");
}
