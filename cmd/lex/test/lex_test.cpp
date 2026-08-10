//
// Unit tests for cmd/lex -- task C10b.  Four kinds: what the tool emits and what
// it refuses, b6lex over cmd/awk/awk.lx.l, a generated scanner compiled by the
// host compiler and run, and eight-bit input with a known answer.
// ../README.md, "Tests".
//
#include <gtest/gtest.h>

#include <algorithm>

#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

extern char **environ;

#ifndef B6LEX_COMMAND
#define B6LEX_COMMAND "b6lex"
#endif
#ifndef B6LEXFORM_PATH
#define B6LEXFORM_PATH "ncform"
#endif
#ifndef B6_SCANNER_DIR
#define B6_SCANNER_DIR "."
#endif
#ifndef HOST_CC
#define HOST_CC "cc"
#endif

// One run of a program: status, and stdout and stderr together.
struct Result {
    int status = -1;
    std::string output;
};

// A scratch directory per test, entered rather than merely created: lex writes
// lex.yy.c under that fixed name in the current directory.  Left behind, since
// lex.yy.c is the first thing to look at on a failure.
class LexTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        const char *tmp  = getenv("TMPDIR");
        std::string base = (tmp && *tmp) ? tmp : "/tmp";
        while (base.size() > 1 && base.back() == '/')
            base.pop_back();

        std::string tmpl = base + "/b6lex-test.XXXXXX";
        std::vector<char> buf(tmpl.c_str(), tmpl.c_str() + tmpl.size() + 1);
        ASSERT_NE(mkdtemp(buf.data()), nullptr) << "cannot create a scratch dir under " << base;
        dir_ = buf.data();

        ASSERT_EQ(getcwd(saved_, sizeof saved_) != nullptr, true);
        ASSERT_EQ(chdir(dir_.c_str()), 0);

        // The skeleton is a source file in this tree, not an installed one.
        ASSERT_EQ(setenv("B6LEXFORM", B6LEXFORM_PATH, 1), 0);
    }

    void TearDown() override
    {
        ASSERT_EQ(chdir(saved_), 0);
    }

    static std::string WriteFile(const std::string &name, const std::string &text)
    {
        std::ofstream f(name, std::ios::trunc | std::ios::binary);
        f << text;
        f.close();
        return name;
    }

    static std::string Slurp(const std::string &name)
    {
        std::ifstream f(name, std::ios::binary);
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    static bool Exists(const std::string &name)
    {
        struct stat st;
        return stat(name.c_str(), &st) == 0;
    }

    // Run a program with output redirected to a file, so there is no pipe to
    // deadlock on, and hand back status and text together.
    static Result Spawn(const std::vector<std::string> &argv, const char *log = "run.log")
    {
        std::vector<char *> cargv(argv.size() + 1, nullptr);
        std::transform(argv.begin(), argv.end(), cargv.begin(),
                       [](const std::string &a) { return const_cast<char *>(a.c_str()); });

        posix_spawn_file_actions_t fa;
        posix_spawn_file_actions_init(&fa);
        posix_spawn_file_actions_addopen(&fa, STDOUT_FILENO, log, O_WRONLY | O_CREAT | O_TRUNC,
                                         0644);
        posix_spawn_file_actions_adddup2(&fa, STDOUT_FILENO, STDERR_FILENO);

        pid_t pid = 0;
        Result r;
        if (posix_spawn(&pid, cargv[0], &fa, nullptr, cargv.data(), environ) != 0) {
            posix_spawn_file_actions_destroy(&fa);
            r.output = std::string("cannot spawn ") + cargv[0];
            return r;
        }
        posix_spawn_file_actions_destroy(&fa);

        int wstatus = 0;
        while (waitpid(pid, &wstatus, 0) < 0 && errno == EINTR)
            ;
        r.status = WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : 128 + WTERMSIG(wstatus);
        r.output = Slurp(log);
        return r;
    }

    static Result Lex(const std::vector<std::string> &args)
    {
        std::vector<std::string> argv{ B6LEX_COMMAND };
        argv.insert(argv.end(), args.begin(), args.end());
        return Spawn(argv);
    }

    // b6lex over a scanner given as text.
    static Result LexText(const std::string &text, const std::vector<std::string> &flags = {})
    {
        WriteFile("s.l", text);
        std::vector<std::string> args(flags);
        args.push_back("s.l");
        return Lex(args);
    }

    // Generate, compile and run a scanner, feeding it `input'.  Returns what the
    // scanner wrote; compilation failures come back in the message.
    static Result Build(const std::string &scanner, const std::string &input)
    {
        Result r = LexText(scanner);
        if (r.status != 0)
            return r;
        Result c = Spawn({ HOST_CC, "-std=c11", "-pedantic-errors", "-Wall", "-Werror",
                           "-Wno-unused-label", "-Wno-unused-but-set-variable", "-o", "scan",
                           "lex.yy.c" },
                         "cc.log");
        if (c.status != 0) {
            c.output = "compiling lex.yy.c failed:\n" + c.output;
            return c;
        }
        WriteFile("in.txt", input);
        return Spawn({ "/bin/sh", "-c", "./scan < in.txt" }, "scan.log");
    }

    std::string dir_;
    char saved_[4096] = { 0 };
};

// The manual page's own example, plus the two functions every scanner owes:
// there is no libl.a on this system.
static const char *kTrivial = R"LEX(%%
[A-Z]	putchar(yytext[0]+'a'-'A');
[ ]+$	;
[ ]+	putchar(' ');
%%
int yywrap(void) { return 1; }
int main(void) { return yylex(); }
)LEX";

// ---------------------------------------------------------------------------
// Emit -- what the tool produces when it is happy
// ---------------------------------------------------------------------------

TEST_F(LexTest, GeneratesAScanner)
{
    Result r = LexText(kTrivial);
    ASSERT_EQ(r.status, 0) << r.output;
    ASSERT_TRUE(Exists("lex.yy.c"));

    std::string out = Slurp("lex.yy.c");
    EXPECT_NE(out.find("int yylex(void)"), std::string::npos) << out;
    EXPECT_NE(out.find("struct yywork"), std::string::npos);
    EXPECT_NE(out.find("yycrank[] ={"), std::string::npos);
    EXPECT_NE(out.find("struct yysvf yysvec[] ={"), std::string::npos);
    EXPECT_NE(out.find("int yyvstop[] ={"), std::string::npos);
    EXPECT_NE(out.find("int yylook(void)"), std::string::npos);
}

// The skeleton is appended last, after the user's own subroutines, so its
// prototypes have to be emitted with the header block -- the same contract
// cmd/yacc has for yaccpar.c.  Assert the ORDER, not merely the presence.
TEST_F(LexTest, EmitsThePrototypesTheSkeletonNeeds)
{
    Result r = LexText(kTrivial);
    ASSERT_EQ(r.status, 0) << r.output;
    std::string out = Slurp("lex.yy.c");

    for (const char *proto : { "int yylook(void);", "int yyback(int *, int);",
                               "int yywrap(void);", "int yyreject(void);", "int yyracc(int);",
                               "void yyless(int);", "int yyinput(void);", "void yyoutput(int);",
                               "void yyunput(int);" }) {
        size_t decl = out.find(proto);
        EXPECT_NE(decl, std::string::npos) << "missing " << proto;
    }
    // ...and each precedes the definition it describes.
    EXPECT_LT(out.find("int yylook(void);"), out.find("int yylook(void)\n{"));
    EXPECT_LT(out.find("int yyback(int *, int);"), out.find("int yyback(int *p, int m)"));
}

// stdin is not a constant expression.  v7 emitted `FILE *yyin ={stdin};', which
// is why no scanner it generated compiles on a modern compiler.
TEST_F(LexTest, StreamsAreNotStaticallyInitialised)
{
    Result r = LexText(kTrivial);
    ASSERT_EQ(r.status, 0) << r.output;
    std::string out = Slurp("lex.yy.c");

    EXPECT_NE(out.find("FILE *yyin, *yyout;"), std::string::npos) << out;
    EXPECT_EQ(out.find("={stdin}"), std::string::npos);
    EXPECT_EQ(out.find("={stdout}"), std::string::npos);
}

// yystoff is a signed int offset into yycrank[]; v7 emitted `yycrank+-5', a
// pointer below the base of its own array.
TEST_F(LexTest, EmitsAnIntegerStateOffset)
{
    Result r = LexText(kTrivial);
    ASSERT_EQ(r.status, 0) << r.output;
    std::string out = Slurp("lex.yy.c");

    EXPECT_NE(out.find("\tint yystoff;"), std::string::npos) << out;
    EXPECT_EQ(out.find("struct yywork *yystoff"), std::string::npos);
    EXPECT_NE(out.find("int yytop = "), std::string::npos);
    EXPECT_EQ(out.find("struct yywork *yytop"), std::string::npos);
    EXPECT_EQ(out.find("yycrank+-"), std::string::npos) << "a pointer below yycrank's base";
}

// The whole byte, and the mask is unconditional: a scanner compiled where char
// is signed would otherwise index yymatch[] from below zero.
TEST_F(LexTest, MasksEveryByteAndCarriesAFullFallbackTable)
{
    Result r = LexText(kTrivial);
    ASSERT_EQ(r.status, 0) << r.output;
    std::string out = Slurp("lex.yy.c");

    EXPECT_NE(out.find("# define U(x) ((x)&0377)"), std::string::npos) << out;
    EXPECT_EQ(out.find("YYU"), std::string::npos) << "the skeleton's own identity macro is gone";
    EXPECT_NE(out.find("unsigned char yymatch[] ={"), std::string::npos);

    // yymatch[] covers all 256 bytes.
    size_t b = out.find("yymatch[] ={");
    ASSERT_NE(b, std::string::npos);
    size_t e = out.find("0};", b);
    ASSERT_NE(e, std::string::npos);
    EXPECT_EQ(std::count(out.begin() + b, out.begin() + e, ','), 256);
}

TEST_F(LexTest, TFlagWritesToStandardOutput)
{
    Result r = LexText(kTrivial, { "-t" });
    ASSERT_EQ(r.status, 0) << r.output;
    EXPECT_FALSE(Exists("lex.yy.c"));
    EXPECT_NE(r.output.find("int yylex(void)"), std::string::npos) << r.output;
}

TEST_F(LexTest, VFlagPrintsOneStatisticsLine)
{
    Result r = LexText(kTrivial, { "-v" });
    ASSERT_EQ(r.status, 0) << r.output;
    EXPECT_NE(r.output.find("nodes(%e)"), std::string::npos) << r.output;
    EXPECT_NE(r.output.find("output slots(%o)"), std::string::npos) << r.output;
    // one line, not v7's two
    EXPECT_EQ(std::count(r.output.begin(), r.output.end(), '\n'), 1) << r.output;
}

TEST_F(LexTest, NFlagIsQuiet)
{
    Result r = LexText(kTrivial, { "-n" });
    ASSERT_EQ(r.status, 0) << r.output;
    EXPECT_EQ(r.output.find("nodes(%e)"), std::string::npos) << r.output;
}

TEST_F(LexTest, FFlagSkipsPacking)
{
    Result r = LexText(kTrivial, { "-f" });
    ASSERT_EQ(r.status, 0) << r.output;
    std::string out = Slurp("lex.yy.c");
    EXPECT_EQ(out.find("# define YYOPTIM 1"), std::string::npos);
    EXPECT_EQ(out.find("yymatch[]"), std::string::npos);
}

TEST_F(LexTest, StartConditionsBecomeDefines)
{
    Result r = LexText(R"LEX(%Start A str
%%
<A>a	;
<str>b	;
%%
int yywrap(void) { return 1; }
int main(void) { return yylex(); }
)LEX");
    ASSERT_EQ(r.status, 0) << r.output;
    std::string out = Slurp("lex.yy.c");
    EXPECT_NE(out.find("# define A 2"), std::string::npos) << out;
    EXPECT_NE(out.find("# define str 4"), std::string::npos) << out;
}

// All six bounds are overridable per-.l, which is the escape hatch a fixed size
// profile usually lacks.  -v reports them as the denominators.
TEST_F(LexTest, BoundOverridesAreReported)
{
    Result r = LexText(R"LEX(%e 640
%n 320
%p 1280
%a 960
%o 1600
%k 480
%%
[a-z]+	;
%%
int yywrap(void) { return 1; }
int main(void) { return yylex(); }
)LEX",
                       { "-v" });
    ASSERT_EQ(r.status, 0) << r.output;
    for (const char *n : { "/640 nodes(%e)", "/1280 positions(%p)", "/320 (%n)",
                           "/480 packed char classes(%k)", "/960 packed transitions(%a)",
                           "/1600 output slots(%o)" })
        EXPECT_NE(r.output.find(n), std::string::npos) << n << " in:\n" << r.output;
}

// ---------------------------------------------------------------------------
// Refuse
// ---------------------------------------------------------------------------

TEST_F(LexTest, MissingInputFileIsFatal)
{
    Result r = Lex({ "nosuch.l" });
    EXPECT_NE(r.status, 0);
    EXPECT_NE(r.output.find("Can't read input file"), std::string::npos) << r.output;
}

// find_form() returns the last candidate rather than a null pointer so that the
// diagnostic can name it.
TEST_F(LexTest, MissingSkeletonNamesThePathItTried)
{
    ASSERT_EQ(setenv("B6LEXFORM", "/nonexistent/ncform", 1), 0);
    Result r = LexText(kTrivial);
    EXPECT_NE(r.status, 0);
    EXPECT_NE(r.output.find("Lex driver missing, file /nonexistent/ncform"), std::string::npos)
        << r.output;
}

TEST_F(LexTest, UnterminatedActionIsFatal)
{
    Result r = LexText("%%\na\t{ printf(\"x\");\n");
    EXPECT_NE(r.status, 0);
    EXPECT_NE(r.output.find("Action does not terminate"), std::string::npos) << r.output;
}

TEST_F(LexTest, EofInsideCommentIsFatal)
{
    Result r = LexText("%%\na\t{ /* forever\n");
    EXPECT_NE(r.status, 0);
    EXPECT_NE(r.output.find("EOF inside comment"), std::string::npos) << r.output;
}

TEST_F(LexTest, UnknownRequestWarns)
{
    Result r = LexText("%q 3\n%%\na\t;\n%%\nint yywrap(void){return 1;}\nint main(void){return yylex();}\n");
    EXPECT_EQ(r.status, 0) << r.output;
    EXPECT_NE(r.output.find("Invalid request"), std::string::npos) << r.output;
}

TEST_F(LexTest, UndefinedDefinitionWarns)
{
    Result r = LexText("%%\n{nosuch}\t;\n%%\nint yywrap(void){return 1;}\nint main(void){return yylex();}\n");
    EXPECT_NE(r.output.find("Definition nosuch not found"), std::string::npos) << r.output;
}

TEST_F(LexTest, TreeOverflowIsFatal)
{
    Result r = LexText("%e 4\n%%\nabcdefghij\t;\n");
    EXPECT_NE(r.status, 0);
    EXPECT_NE(r.output.find("Parse tree too big"), std::string::npos) << r.output;
}

TEST_F(LexTest, UnknownOptionWarnsAndCarriesOn)
{
    Result r = LexText(kTrivial, { "-Z" });
    EXPECT_EQ(r.status, 0) << r.output;
    EXPECT_NE(r.output.find("Unknown option Z"), std::string::npos) << r.output;
}

// The Ratfor half really left, rather than merely becoming unreachable.
TEST_F(LexTest, RatforIsGone)
{
    Result r = LexText(kTrivial, { "-r" });
    EXPECT_NE(r.output.find("Unknown option r"), std::string::npos) << r.output;

    Result q = LexText("%r\n%%\na\t;\n%%\nint yywrap(void){return 1;}\nint main(void){return yylex();}\n");
    EXPECT_NE(q.output.find("Invalid request"), std::string::npos) << q.output;
}

// ---------------------------------------------------------------------------
// The one scanner this machine has to compile
// ---------------------------------------------------------------------------

// awk.lx.l cannot be COMPILED here -- it wants awk.h, awk.def and awk's own
// main() -- but it must generate, and its numbers are pinned the way
// cmd/yacc/README.md pins the six grammars' conflict counts: a number that moves
// means lex or the scanner changed, and that is a stop rather than a shrug.
TEST_F(LexTest, AwkScannerGenerates)
{
    Result r = Lex({ "-v", std::string(B6_SCANNER_DIR) + "/awk/awk.lx.l" });
    ASSERT_EQ(r.status, 0) << r.output;
    ASSERT_TRUE(Exists("lex.yy.c"));
    EXPECT_GT(Slurp("lex.yy.c").size(), 1000u);

    // Measured, not guessed.  Every numerator is what awk costs and every
    // denominator the bound ldefs.h sets; task C10d sizes the `besm6' profile
    // from exactly these numbers.
    EXPECT_EQ(r.output, "618/1000 nodes(%e), 1345/2500 positions(%p), 202/500 (%n), "
                        "9663 transitions, 64/1000 packed char classes(%k), "
                        "530/2000 packed transitions(%a), 455/3000 output slots(%o)\n")
        << r.output;
}

// ---------------------------------------------------------------------------
// A generated scanner, compiled and run
// ---------------------------------------------------------------------------

TEST_F(LexTest, GeneratedScannerCompilesAndRuns)
{
    Result r = Build(kTrivial, "AB  C  \nD\n");
    ASSERT_EQ(r.status, 0) << r.output;
    EXPECT_EQ(r.output, "ab c\nd\n");
}

TEST_F(LexTest, GeneratedScannerHandlesStartConditions)
{
    // A qualified rule wins over an unqualified one only because it is listed
    // first -- an unqualified lex rule is active in EVERY start condition.  The
    // switch on yybgin-yysvec-1 is cmd/awk's idiom and must still compile.
    Result r = Build(R"LEX(%Start TWO
%%
a	{ printf("a"); BEGIN TWO; }
<TWO>b	{ printf("B"); BEGIN INITIAL; }
b	{ printf("b"); }
\n	{ printf("|%d", (int)(yybgin-yysvec-1)); }
%%
int yywrap(void) { return 1; }
int main(void) { return yylex(); }
)LEX",
                     "abb\n");
    ASSERT_EQ(r.status, 0) << r.output;
    EXPECT_EQ(r.output, "aBb|0");
}

// THE case decision 2 needs: a state packed by character, so yystoff is negative
// and the skeleton's reflected-offset arm actually executes.  Assert both that
// one was emitted and that the scanner still classifies correctly.
TEST_F(LexTest, GeneratedScannerWalksACompressedState)
{
    static const char *scanner = R"LEX(%%
[a-z]+	printf("L%d", yyleng);
[A-Z]+	printf("U%d", yyleng);
[0-9]+	printf("D%d", yyleng);
\n	printf("\n");
.	printf("?");
%%
int yywrap(void) { return 1; }
int main(void) { return yylex(); }
)LEX";
    Result g = LexText(scanner);
    ASSERT_EQ(g.status, 0) << g.output;

    // a yysvec[] row whose offset is negative
    std::string out = Slurp("lex.yy.c");
    size_t b = out.find("struct yysvf yysvec[] ={");
    ASSERT_NE(b, std::string::npos);
    size_t e = out.find("{0,\t0,\t0}};", b);
    ASSERT_NE(e, std::string::npos);
    size_t neg = out.find("{-", b);
    EXPECT_TRUE(neg != std::string::npos && neg < e)
        << "no char-compressed state in:\n" << out.substr(b, e - b);

    Result r = Build(scanner, "abc XY 42 +\n");
    ASSERT_EQ(r.status, 0) << r.output;
    EXPECT_EQ(r.output, "L3?U2?D2??\n");
}

// What decision 4 buys: REJECT and yyless() come from the skeleton, so a scanner
// using either links with no -ll.
TEST_F(LexTest, GeneratedScannerRejects)
{
    Result r = Build(R"LEX(%%
abc	{ printf("[abc]"); REJECT; }
ab	{ printf("[ab]"); }
c	{ printf("[c]"); }
\n	;
%%
int yywrap(void) { return 1; }
int main(void) { return yylex(); }
)LEX",
                     "abc\n");
    ASSERT_EQ(r.status, 0) << r.output;
    EXPECT_EQ(r.output, "[abc][ab][c]");
}

TEST_F(LexTest, GeneratedScannerYyless)
{
    Result r = Build(R"LEX(%%
abc	{ printf("<%s>", yytext); yyless(1); }
[a-z]	{ printf("(%s)", yytext); }
\n	;
%%
int yywrap(void) { return 1; }
int main(void) { return yylex(); }
)LEX",
                     "abc\n");
    ASSERT_EQ(r.status, 0) << r.output;
    EXPECT_EQ(r.output, "<abc>(b)(c)");
}

// ---------------------------------------------------------------------------
// Eight bits, with a known answer rather than an eyeball
// ---------------------------------------------------------------------------

// A literal built from bytes above 127 must match those bytes.  A lex that
// masked to seven bits would have built the rule from 'P' and '+', so it would
// not match -- and the fall-through prints the offending bytes BY VALUE, which
// is what makes the failure legible.  The rule is written twice, once with the
// bytes themselves and once with lex's octal escapes, because those are two
// different paths through yylex() and both have to carry eight bits.
//
// This file stays pure ASCII -- cppcheck cannot parse a source that is not --
// so the bytes reach the .l through a C escape.  D0 AB is CYRILLIC CAPITAL YERU.
TEST_F(LexTest, EightBitLiteralMatchesItsOwnBytes)
{
    const std::string raw = "%%\n"
                            "\"\xD0\xAB\"\tprintf(\"cyr\");\n"
                            "\\n\tprintf(\"\\n\");\n"
                            ".\tprintf(\"%d,\", (unsigned char)yytext[0]);\n"
                            "%%\n"
                            "int yywrap(void) { return 1; }\n"
                            "int main(void) { return yylex(); }\n";
    const std::string octal = "%%\n"
                              "\"\\320\\253\"\tprintf(\"cyr\");\n"
                              "\\n\tprintf(\"\\n\");\n"
                              ".\tprintf(\"%d,\", (unsigned char)yytext[0]);\n"
                              "%%\n"
                              "int yywrap(void) { return 1; }\n"
                              "int main(void) { return yylex(); }\n";

    for (const std::string &scanner : { raw, octal }) {
        Result r = Build(scanner, "\xD0\xAB\n");
        ASSERT_EQ(r.status, 0) << r.output;
        EXPECT_EQ(r.output, "cyr\n");

        // ...and the negative control: the seven-bit folding of those two bytes
        // is `P+', which must NOT match the rule.  Without this, a lex that
        // masked both the rule and the input would pass the case above.
        Result q = Build(scanner, "P+\n");
        ASSERT_EQ(q.status, 0) << q.output;
        EXPECT_EQ(q.output, "80,43,\n");
    }
}

// A character class over the high half: this is what overran symbol[] when NCH
// was 128, and the answer is a count rather than a look.
TEST_F(LexTest, EightBitClassCountsBytes)
{
    Result r = Build(R"LEX(%%
[\200-\377]+	printf("hi%d", yyleng);
[a-z]+	printf("lo%d", yyleng);
\n	printf("\n");
%%
int yywrap(void) { return 1; }
int main(void) { return yylex(); }
)LEX",
                     "\xD0\xAB\xD0\xBF abc\n");
    ASSERT_EQ(r.status, 0) << r.output;
    EXPECT_EQ(r.output, "hi4 lo3\n");
}
