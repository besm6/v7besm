//
// End-to-end test for the b6cc compiler driver.
//
// It writes a tiny C program to a throwaway temp directory, runs the built
// b6cc with -S, and checks that the generated Madlen assembly contains the
// expected landmarks.  The preprocessor is pinned to the freshly built b6cpp
// via the B6CPP override; the three compiler passes (b6parse/b6lower/
// b6codegen) are resolved by the driver from ~/.local/bin or /usr/local/bin.
// When those passes are not installed, the test skips rather than fails.
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

        // Pin the preprocessor to the freshly built b6cpp.
        ASSERT_EQ(setenv("B6CPP", B6CPP_PATH, 1), 0);
    }

    void TearDown() override
    {
        if (!dir.empty()) {
            // Best-effort recursive removal of the temp directory.
            std::string cmd = "rm -rf '" + dir + "'";
            ::system(cmd.c_str());
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
    EXPECT_NE(text.find(",end,"), std::string::npos) << text;
    EXPECT_NE(text.find("b/save"), std::string::npos) << text;
}

} // namespace
