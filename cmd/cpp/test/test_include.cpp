// C11 §6.10.2 — Source file inclusion.
#include "test_support.h"

using Include = c11pp::PreprocessorTest;

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
