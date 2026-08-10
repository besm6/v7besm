//
// Unit tests for cmd/yacc -- task C10a.  Three kinds: what the tool refuses and
// emits, b6yacc over the six grammars tasks C11-C17 will feed it, and a
// generated parser compiled and run.  ../README.md, "Tests".
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

#ifndef B6YACC_COMMAND
#define B6YACC_COMMAND "b6yacc"
#endif
#ifndef B6YACCPAR_PATH
#define B6YACCPAR_PATH "yaccpar.c"
#endif
#ifndef B6_GRAMMAR_DIR
#define B6_GRAMMAR_DIR "."
#endif
#ifndef HOST_CC
#define HOST_CC "cc"
#endif

// One run of a program: status, and stdout and stderr together.
struct Result {
    int status = -1;
    std::string output;
};

// A scratch directory per test, entered rather than merely created: yacc writes
// its three outputs under fixed names in the current directory.  Left behind,
// since y.tab.c and y.output are the first things to look at on a failure.
class YaccTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        const char *tmp = getenv("TMPDIR");
        std::string base = (tmp && *tmp) ? tmp : "/tmp";
        while (base.size() > 1 && base.back() == '/')
            base.pop_back();

        std::string tmpl = base + "/b6yacc-test.XXXXXX";
        std::vector<char> buf(tmpl.c_str(), tmpl.c_str() + tmpl.size() + 1);
        ASSERT_NE(mkdtemp(buf.data()), nullptr) << "cannot create a scratch dir under " << base;
        dir_ = buf.data();

        ASSERT_EQ(getcwd(saved_, sizeof saved_) != nullptr, true);
        ASSERT_EQ(chdir(dir_.c_str()), 0);

        // The skeleton is a source file in this tree, not an installed one.
        ASSERT_EQ(setenv("B6YACCPAR", B6YACCPAR_PATH, 1), 0);
    }

    void TearDown() override
    {
        ASSERT_EQ(chdir(saved_), 0);
    }

    // Write a grammar into the scratch directory and return its name.
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
    static Result Spawn(const std::vector<std::string> &argv)
    {
        std::vector<char *> cargv(argv.size() + 1, nullptr);
        std::transform(argv.begin(), argv.end(), cargv.begin(),
                       [](const std::string &a) { return const_cast<char *>(a.c_str()); });

        const char *log = "run.log";
        posix_spawn_file_actions_t fa;
        posix_spawn_file_actions_init(&fa);
        posix_spawn_file_actions_addopen(&fa, STDOUT_FILENO, log,
                                         O_WRONLY | O_CREAT | O_TRUNC, 0644);
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

    // b6yacc over a grammar already written into the scratch directory.
    static Result Yacc(const std::vector<std::string> &args)
    {
        std::vector<std::string> argv{ B6YACC_COMMAND };
        argv.insert(argv.end(), args.begin(), args.end());
        return Spawn(argv);
    }

    // b6yacc over a grammar given as text.
    static Result YaccText(const std::string &text, const std::vector<std::string> &flags = {})
    {
        WriteFile("g.y", text);
        std::vector<std::string> args(flags);
        args.push_back("g.y");
        return Yacc(args);
    }

    std::string dir_;
    char saved_[4096] = { 0 };
};

// A grammar that is correct, tiny, and has no conflicts.
static const char *kTrivial = R"(%token NUMBER
%%
list	:
	| list NUMBER
	;
%%
int yylex(void) { return 0; }
void yyerror(char *s) { (void)s; }
int main(void) { return yyparse(); }
)";

// ---------------------------------------------------------------------------
// Generate -- what the tool emits when it is happy
// ---------------------------------------------------------------------------

TEST_F(YaccTest, GeneratesATableFile)
{
    Result r = YaccText(kTrivial);
    ASSERT_EQ(r.status, 0) << r.output;
    ASSERT_TRUE(Exists("y.tab.c"));

    std::string tab = Slurp("y.tab.c");
    EXPECT_NE(tab.find("int yyparse(void)"), std::string::npos);
    EXPECT_NE(tab.find("short yyact[]"), std::string::npos);
    EXPECT_NE(tab.find("short yypact[]"), std::string::npos);
}

// The prototypes y2.c emits ahead of the user's own subroutines -- the contract
// every grammar in this tree meets.
TEST_F(YaccTest, EmitsThePrototypesTheSkeletonNeeds)
{
    Result r = YaccText(kTrivial);
    ASSERT_EQ(r.status, 0) << r.output;

    std::string tab = Slurp("y.tab.c");
    EXPECT_NE(tab.find("int yylex(void);"), std::string::npos);
    EXPECT_NE(tab.find("void yyerror(char *);"), std::string::npos);
    EXPECT_NE(tab.find("int yyparse(void);"), std::string::npos);

    // ...and they must precede the user's section-3 code, or they would be
    // redeclaring what has already been defined.
    EXPECT_LT(tab.find("int yylex(void);"), tab.find("int yylex(void) { return 0; }"));
}

TEST_F(YaccTest, DFlagWritesTokenDefinitions)
{
    Result r = YaccText(kTrivial, { "-d" });
    ASSERT_EQ(r.status, 0) << r.output;
    ASSERT_TRUE(Exists("y.tab.h"));
    EXPECT_NE(Slurp("y.tab.h").find("# define NUMBER 257"), std::string::npos);
}

TEST_F(YaccTest, VFlagWritesTheReport)
{
    Result r = YaccText(kTrivial, { "-v" });
    ASSERT_EQ(r.status, 0) << r.output;
    ASSERT_TRUE(Exists("y.output"));
    EXPECT_NE(Slurp("y.output").find("terminals"), std::string::npos);
}

// The tmpfile() conversion.  These two names were the manual page's BUGS
// paragraph and are gone.
TEST_F(YaccTest, LeavesNoTemporaryFilesBehind)
{
    Result r = YaccText(kTrivial);
    ASSERT_EQ(r.status, 0) << r.output;
    EXPECT_FALSE(Exists("yacc.tmp"));
    EXPECT_FALSE(Exists("yacc.acts"));
}

// The classic ambiguous expression, whose one shift/reduce conflict is the
// reason %left exists.
TEST_F(YaccTest, ReportsShiftReduceConflicts)
{
    Result r = YaccText(R"(%token NUMBER
%%
e	: e '+' e
	| NUMBER
	;
)");
    ASSERT_EQ(r.status, 0) << r.output;
    EXPECT_NE(r.output.find("shift/reduce"), std::string::npos) << r.output;
}

TEST_F(YaccTest, PrecedenceRemovesTheConflict)
{
    Result r = YaccText(R"(%token NUMBER
%left '+'
%%
e	: e '+' e
	| NUMBER
	;
)");
    ASSERT_EQ(r.status, 0) << r.output;
    EXPECT_EQ(r.output.find("conflicts"), std::string::npos) << r.output;
}

// ---------------------------------------------------------------------------
// Diagnose -- what it refuses, and how
// ---------------------------------------------------------------------------

TEST_F(YaccTest, UndefinedNonterminalIsFatal)
{
    Result r = YaccText(R"(%%
a	: b
	;
)");
    EXPECT_NE(r.status, 0);
    EXPECT_NE(r.output.find("not defined"), std::string::npos) << r.output;
}

TEST_F(YaccTest, MissingMarkIsFatal)
{
    Result r = YaccText("%token NUMBER\n");
    EXPECT_NE(r.status, 0);
    EXPECT_NE(r.output.find("unexpected EOF"), std::string::npos) << r.output;
}

TEST_F(YaccTest, BadFirstRuleIsFatal)
{
    Result r = YaccText("%%\n%token X\n");
    EXPECT_NE(r.status, 0);
    EXPECT_NE(r.output.find("bad syntax on first rule"), std::string::npos) << r.output;
}

TEST_F(YaccTest, TokenOnTheLeftIsFatal)
{
    Result r = YaccText("%%\n'a' : 'b' ;\n");
    EXPECT_NE(r.status, 0);
    EXPECT_NE(r.output.find("token illegal on LHS"), std::string::npos) << r.output;
}

TEST_F(YaccTest, IllegalOptionIsFatal)
{
    Result r = YaccText(kTrivial, { "-q" });
    EXPECT_NE(r.status, 0);
    EXPECT_NE(r.output.find("illegal option"), std::string::npos) << r.output;
}

TEST_F(YaccTest, MissingInputFileIsFatal)
{
    Result r = Yacc({ "no-such-grammar.y" });
    EXPECT_NE(r.status, 0);
    EXPECT_NE(r.output.find("cannot open input file"), std::string::npos) << r.output;
}

// find_parser() returns the last candidate rather than a null pointer so that
// the diagnostic can name it.
TEST_F(YaccTest, MissingSkeletonNamesThePathItTried)
{
    ASSERT_EQ(setenv("B6YACCPAR", "/nonexistent/yaccpar.c", 1), 0);
    Result r = YaccText(kTrivial);
    EXPECT_NE(r.status, 0);
    EXPECT_NE(r.output.find("cannot find parser /nonexistent/yaccpar.c"), std::string::npos)
        << r.output;
}

// ---------------------------------------------------------------------------
// Grammars -- the seven this yacc exists to compile
// ---------------------------------------------------------------------------

namespace {

struct GrammarCase {
    const char *path;    // relative to cmd/
    int shift_reduce;    // conflicts summary() reports
    int reduce_reduce;
};

// Measured, not guessed.  A number moving means the generator or the grammar did.
const GrammarCase kGrammars[] = {
    { "expr/expr.y", 0, 0 },   { "egrep/egrep.y", 2, 0 }, { "m4/m4y.y", 0, 0 },
    { "make/gram.y", 0, 0 },   { "bc/bc.y", 12, 30 },     { "awk/awk.g.y", 95, 0 },
};

} // namespace

class GrammarTest : public YaccTest, public ::testing::WithParamInterface<GrammarCase> {};

TEST_P(GrammarTest, Generates)
{
    const GrammarCase &g = GetParam();
    std::string path = std::string(B6_GRAMMAR_DIR) + "/" + g.path;

    Result r = Yacc({ "-vd", path });
    ASSERT_EQ(r.status, 0) << r.output;
    ASSERT_TRUE(Exists("y.tab.c"));
    EXPECT_GT(Slurp("y.tab.c").size(), 1000u);

    if (g.shift_reduce || g.reduce_reduce) {
        std::string want = "conflicts: ";
        if (g.shift_reduce)
            want += std::to_string(g.shift_reduce) + " shift/reduce";
        if (g.shift_reduce && g.reduce_reduce)
            want += ", ";
        if (g.reduce_reduce)
            want += std::to_string(g.reduce_reduce) + " reduce/reduce";
        EXPECT_NE(r.output.find(want), std::string::npos) << r.output;
    } else {
        EXPECT_EQ(r.output.find("conflicts:"), std::string::npos) << r.output;
    }
}

INSTANTIATE_TEST_SUITE_P(Ported, GrammarTest, ::testing::ValuesIn(kGrammars),
                         [](const ::testing::TestParamInfo<GrammarCase> &i) {
                             std::string n = i.param.path;
                             for (char &c : n)
                                 if (!isalnum((unsigned char)c))
                                     c = '_';
                             return n;
                         });

// ---------------------------------------------------------------------------
// EndToEnd -- generate, compile, run
// ---------------------------------------------------------------------------

// -pedantic-errors so that anything b6parse would refuse for being K&R is an
// error here too.  The two -Wno- flags are properties of yacc's output, not of
// this skeleton: yyerrlab is unreferenced unless the grammar uses YYERROR, and
// yypvt is set but unused unless an action names $n.
TEST_F(YaccTest, GeneratedParserCompilesAndRuns)
{
    Result r = YaccText(R"(%token NUMBER
%left '+' '-'
%%
list	:
	| list e '\n'		{ printf("%d\n", $2); }
	;
e	: e '+' e		{ $$ = $1 + $3; }
	| e '-' e		{ $$ = $1 - $3; }
	| NUMBER
	;
%%
#include <stdio.h>

int yylex(void)
{
	int c = getchar();
	if (c == EOF)
		return 0;
	if (c >= '0' && c <= '9') {
		yylval = c - '0';
		return NUMBER;
	}
	return c;
}

void yyerror(char *s)
{
	printf("%s\n", s);
}

int main(void)
{
	return yyparse();
}
)");
    ASSERT_EQ(r.status, 0) << r.output;

    Result cc = Spawn({ HOST_CC, "-std=c11", "-pedantic-errors", "-Wall", "-Werror",
                        "-Wno-unused-label", "-Wno-unused-but-set-variable", "-o", "calc", "y.tab.c" });
    ASSERT_EQ(cc.status, 0) << cc.output;

    WriteFile("in", "1+2-4\n7-3\n");
    Result run = Spawn({ "/bin/sh", "-c", "./calc < in" });
    EXPECT_EQ(run.status, 0) << run.output;
    EXPECT_EQ(run.output, "-1\n4\n");
}

// The error recovery path, which is where the yyps/yypv sentinel lives: the pop
// loop is the only place v7's below-the-array pointer was reached in anger.
TEST_F(YaccTest, GeneratedParserRecoversFromAnError)
{
    Result r = YaccText(R"(%token NUMBER
%%
list	:
	| list NUMBER '\n'	{ printf("ok\n"); }
	| list error '\n'	{ printf("bad\n"); yyerrok; }
	;
%%
#include <stdio.h>

int yylex(void)
{
	int c = getchar();
	if (c == EOF)
		return 0;
	if (c >= '0' && c <= '9')
		return NUMBER;
	return c;
}

void yyerror(char *s)
{
	(void)s;
}

int main(void)
{
	return yyparse();
}
)");
    ASSERT_EQ(r.status, 0) << r.output;

    Result cc = Spawn({ HOST_CC, "-std=c11", "-pedantic-errors", "-Wall", "-Werror",
                        "-Wno-unused-label", "-Wno-unused-but-set-variable", "-o", "rec", "y.tab.c" });
    ASSERT_EQ(cc.status, 0) << cc.output;

    WriteFile("in", "1\nxx\n2\n");
    Result run = Spawn({ "/bin/sh", "-c", "./rec < in" });
    EXPECT_EQ(run.output, "ok\nbad\nok\n") << run.output;
}
