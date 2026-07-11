//
// End-to-end test for the b6cc compiler driver.
//
// It writes a tiny C program to a throwaway temp directory, runs the built
// b6cc with -S, and checks that the generated Madlen assembly contains the
// expected landmarks.  The preprocessor and assembler are pinned to the freshly
// built b6cpp/b6as via the B6CPP/B6AS overrides; the three compiler passes
// (b6parse/b6lower/b6codegen) are resolved by the driver from ~/.local/bin or
// /usr/local/bin.  When those passes are not installed, the C test skips rather
// than fails.  The .S (preprocessed assembly) path uses only b6cpp and b6as, so
// its tests run without the external passes.
//
#include <gtest/gtest.h>

#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

extern char **environ;

namespace {

// Locate a sub-tool the same way the driver does: ~/.local/bin then
// /usr/local/bin.  Returns an empty string if not found.
std::string FindTool(const std::string &name)
{
    std::vector<std::string> dirs;
    if (const char *home = getenv("HOME"))
        dirs.push_back(std::string(home) + "/.local/bin");
    dirs.push_back("/usr/local/bin");

    for (const auto &dir : dirs) {
        std::string path = dir + "/" + name;
        if (access(path.c_str(), X_OK) == 0)
            return path;
    }
    return {};
}

// Read an entire file into a string.
std::string ReadFile(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Run argv (NULL-terminated) and return the child's exit code, or -1 on spawn
// failure / abnormal termination.
int RunProcess(const std::vector<std::string> &argv)
{
    std::vector<char *> cargv;
    for (const auto &a : argv)
        // cppcheck-suppress useStlAlgorithm
        cargv.push_back(const_cast<char *>(a.c_str()));
    cargv.push_back(nullptr);

    pid_t pid;
    if (posix_spawn(&pid, cargv[0], nullptr, nullptr, cargv.data(), environ) != 0)
        return -1;

    int status;
    if (waitpid(pid, &status, 0) < 0)
        return -1;
    if (!WIFEXITED(status))
        return -1;
    return WEXITSTATUS(status);
}

class CcDriver : public ::testing::Test {
protected:
    std::string dir;

    void SetUp() override
    {
        char tmpl[] = "/tmp/cc_testXXXXXX";
        ASSERT_NE(mkdtemp(tmpl), nullptr) << "mkdtemp failed";
        dir = tmpl;

        // Pin the preprocessor and assembler to the freshly built tools.
        ASSERT_EQ(setenv("B6CPP", B6CPP_PATH, 1), 0);
        ASSERT_EQ(setenv("B6AS", B6AS_PATH, 1), 0);
    }

    void TearDown() override
    {
        if (!dir.empty()) {
            // Best-effort recursive removal of the temp directory.
            std::string cmd = "rm -rf '" + dir + "'";
            ASSERT_EQ(::system(cmd.c_str()), 0);
        }
    }

    void WriteSource(const std::string &name, const std::string &content)
    {
        std::ofstream out(dir + "/" + name, std::ios::binary | std::ios::trunc);
        out << content;
    }
};

TEST_F(CcDriver, CompileToAssembly)
{
    // The three compiler passes come from the sibling c-compiler install; skip
    // cleanly if they are not present on this machine.
    for (const char *tool : { "b6parse", "b6lower", "b6codegen" }) {
        if (FindTool(tool).empty())
            GTEST_SKIP() << tool << " not installed; skipping end-to-end test";
    }

    WriteSource("t.c", "int main(void)\n{\n    return 42;\n}\n");

    std::string src = dir + "/t.c";
    std::string asm_out = dir + "/t.s";
    ASSERT_EQ(RunProcess({ B6CC_COMMAND, "-S", "-o", asm_out, src }), 0)
        << "b6cc -S failed";

    std::string text = ReadFile(asm_out);
    EXPECT_FALSE(text.empty()) << "assembly output is empty";
    // Landmarks from the BESM-6 codegen for a trivial main() (=52 is octal 42).
    EXPECT_NE(text.find("main:"), std::string::npos) << text;
    EXPECT_NE(text.find("b$save"), std::string::npos) << text;
    EXPECT_NE(text.find("#052"), std::string::npos) << text;
    EXPECT_NE(text.find("b$ret"), std::string::npos) << text;
}

// -Smadlen stops after codegen like -S but selects the Madlen (Dubna) dialect,
// which differs from the default Unix (b6as) assembly emitted by plain -S.
TEST_F(CcDriver, CompileToAssemblyMadlen)
{
    for (const char *tool : { "b6parse", "b6lower", "b6codegen" }) {
        if (FindTool(tool).empty())
            GTEST_SKIP() << tool << " not installed; skipping end-to-end test";
    }

    WriteSource("t.c", "int main(void)\n{\n    return 42;\n}\n");

    std::string src = dir + "/t.c";
    std::string unix_out = dir + "/u.s";
    std::string madlen_out = dir + "/m.s";
    ASSERT_EQ(RunProcess({ B6CC_COMMAND, "-S", "-o", unix_out, src }), 0)
        << "b6cc -S failed";
    ASSERT_EQ(RunProcess({ B6CC_COMMAND, "-Smadlen", "-o", madlen_out, src }), 0)
        << "b6cc -Smadlen failed";

    std::string madlen = ReadFile(madlen_out);
    EXPECT_FALSE(madlen.empty()) << "madlen assembly output is empty";
    EXPECT_NE(madlen.find("main:"), std::string::npos) << madlen;
    // The ,end, directive is distinctive of the Madlen dialect.
    EXPECT_NE(madlen.find(",end,"), std::string::npos) << madlen;
    // And it differs from the default Unix assembly emitted by plain -S.
    EXPECT_NE(madlen, ReadFile(unix_out)) << "madlen output matches unix -S";
}

// -Sbemsh stops after codegen like -S but selects the Bemsh (Cyrillic
// autocode) dialect, so its output differs from the default Unix -S assembly.
TEST_F(CcDriver, CompileToAssemblyBemsh)
{
    for (const char *tool : { "b6parse", "b6lower", "b6codegen" }) {
        if (FindTool(tool).empty())
            GTEST_SKIP() << tool << " not installed; skipping end-to-end test";
    }

    WriteSource("t.c", "int main(void)\n{\n    return 42;\n}\n");

    std::string src = dir + "/t.c";
    std::string unix_out = dir + "/u.s";
    std::string bemsh_out = dir + "/b.s";
    ASSERT_EQ(RunProcess({ B6CC_COMMAND, "-S", "-o", unix_out, src }), 0)
        << "b6cc -S failed";
    ASSERT_EQ(RunProcess({ B6CC_COMMAND, "-Sbemsh", "-o", bemsh_out, src }), 0)
        << "b6cc -Sbemsh failed";

    std::string bemsh = ReadFile(bemsh_out);
    EXPECT_FALSE(bemsh.empty()) << "bemsh assembly output is empty";
    // The Bemsh autocode uses Cyrillic keywords: "старт" (start) / "финиш".
    EXPECT_NE(bemsh.find("\xD1\x81\xD1\x82\xD0\xB0\xD1\x80\xD1\x82"), std::string::npos)
        << bemsh;
    // And it differs from the default Unix assembly emitted by plain -S.
    EXPECT_NE(bemsh, ReadFile(unix_out)) << "bemsh output matches unix -S";
}

// Without -o, the dialect selectors derive the output name with a matching
// extension: t.c -> t.bemsh for -Sbemsh, t.madlen for -Smadlen.
TEST_F(CcDriver, DialectDerivesExtension)
{
    for (const char *tool : { "b6parse", "b6lower", "b6codegen" }) {
        if (FindTool(tool).empty())
            GTEST_SKIP() << tool << " not installed; skipping end-to-end test";
    }

    WriteSource("t.c", "int main(void)\n{\n    return 0;\n}\n");

    // b6cc derives the output name relative to the cwd, so run from `dir`.
    ASSERT_EQ(RunProcess({ "/bin/sh", "-c",
                           std::string("cd '") + dir + "' && '" + B6CC_COMMAND +
                               "' -Sbemsh t.c && '" + B6CC_COMMAND + "' -Smadlen t.c" }),
              0)
        << "b6cc dialect compile failed";

    EXPECT_FALSE(ReadFile(dir + "/t.bemsh").empty()) << "t.bemsh not produced";
    EXPECT_FALSE(ReadFile(dir + "/t.madlen").empty()) << "t.madlen not produced";
}

// An unknown -S suffix is rejected with a usage error.
TEST_F(CcDriver, RejectsUnknownDialect)
{
    WriteSource("t.c", "int main(void)\n{\n    return 0;\n}\n");
    std::string src = dir + "/t.c";
    EXPECT_NE(RunProcess({ B6CC_COMMAND, "-Sfoo", src }), 0)
        << "b6cc -Sfoo should fail";
}

// A .S file is assembly that must be run through the C preprocessor first.  With
// -E the driver stops after b6cpp, so this exercises the cpp half of the path
// using only the pinned b6cpp.
TEST_F(CcDriver, PreprocessDotS)
{
    WriteSource("x.S",
                "#define REG 0\n"
                "#ifdef REG\n"
                "        xta REG\n"
                "#endif\n");

    std::string src = dir + "/x.S";
    std::string out = dir + "/x.i";
    ASSERT_EQ(RunProcess({ B6CC_COMMAND, "-E", "-o", out, src }), 0)
        << "b6cc -E on .S failed";

    std::string text = ReadFile(out);
    // The macro was expanded and the directives are gone.
    EXPECT_NE(text.find("xta 0"), std::string::npos) << text;
    EXPECT_EQ(text.find("#define"), std::string::npos) << text;
    EXPECT_EQ(text.find("#ifdef"), std::string::npos) << text;
}

// The full .S path: cpp -> as -> object.  Uses only the in-tree b6cpp and b6as,
// so it does not depend on the external c-compiler passes.
TEST_F(CcDriver, AssembleDotS)
{
    WriteSource("x.S",
                "#define REG 0\n"
                "        xta REG\n"
                "        atx REG\n");

    std::string src = dir + "/x.S";
    std::string obj = dir + "/x.o";
    ASSERT_EQ(RunProcess({ B6CC_COMMAND, "-c", "-o", obj, src }), 0)
        << "b6cc -c on .S failed";

    std::string bytes = ReadFile(obj);
    EXPECT_FALSE(bytes.empty()) << "object file is empty";
}

} // namespace
