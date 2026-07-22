// C11 §6.10.2 — Source file inclusion.
#include "test_support.h"

using Include = PreprocessorTest;

// #include "..." finds a file next to the including source.
TEST_F(Include, QuotedForm) {
    EXPECT_TRUE(TokensAre(
        "#include \"local.h\"\n"
        "GREETING\n",
        "hi",
        {},
        {{"local.h", "#define GREETING hi\n"}}));
}

// #include <...> searches the include path (harness passes -I<tempdir>).
TEST_F(Include, AngleForm) {
    EXPECT_TRUE(TokensAre(
        "#include <sys.h>\n"
        "SYSVAL\n",
        "42",
        {},
        {{"sys.h", "#define SYSVAL 42\n"}}));
}

// §6.10.2p4 matches the <h-char-sequence> form BEFORE macro expansion, so a
// live OBJECT-like macro whose name is the header's basename must not rewrite
// it.  This used to include bar.h and fail to find it.
TEST_F(Include, AngleFormNotMacroExpanded) {
    EXPECT_TRUE(TokensAre(
        "#define sys bar\n"
        "#include <sys.h>\n"
        "SYSVAL\n",
        "42",
        {},
        {{"sys.h", "#define SYSVAL 42\n"}}));
}

// The same, with a FUNCTION-like macro of that name -- the case that bit
// <assert.h>, which defines assert and is deliberately not include-guarded, so
// a second inclusion of it is meant to work.  This used to emit the directive
// as garbage text and drop the inclusion entirely, taking the NEXT directive's
// '#' with it.
TEST_F(Include, AngleFormNotExpandedByFunctionMacro) {
    EXPECT_TRUE(TokensAre(
        "#define sys(x) ((x) + 1)\n"
        "#include <sys.h>\n"
        "SYSVAL\n",
        "42",
        {},
        {{"sys.h", "#define SYSVAL 42\n"}}));
}

// The quoted form is literal for the same reason.
TEST_F(Include, QuotedFormNotMacroExpanded) {
    EXPECT_TRUE(TokensAre(
        "#define local bar\n"
        "#include \"local.h\"\n"
        "GREETING\n",
        "hi",
        {},
        {{"local.h", "#define GREETING hi\n"}}));
}

// §6.10.2p4: the macro-expanded form of #include (computed include).
TEST_F(Include, Computed) {
    EXPECT_TRUE(TokensAre(
        "#define HDR \"local.h\"\n"
        "#include HDR\n"
        "GREETING\n",
        "hi",
        {},
        {{"local.h", "#define GREETING hi\n"}}));
}

// An include-guarded header included twice yields its body only once.
TEST_F(Include, GuardIsIdempotent) {
    EXPECT_TRUE(TokensAre(
        "#include \"g.h\"\n"
        "#include \"g.h\"\n",
        "body",
        {},
        {{"g.h", "#ifndef G_H\n#define G_H\nbody\n#endif\n"}}));
}

// __FILE__/__LINE__ reflect the included file, then restore on return.
TEST_F(Include, LineAndFileTracking) {
    // inc.h line 1 emits its own __LINE__ (1); back in the main file, __LINE__
    // sits on line 2.
    Result r = Preprocess(
        "#include \"inc.h\"\n"
        "__LINE__\n",
        {},
        {{"inc.h", "__LINE__\n"}});
    ASSERT_EQ(r.exit_code, 0) << r.err;
    EXPECT_EQ(Normalize(r.out), "1 2");
}
