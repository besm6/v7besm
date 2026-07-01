//
// End-to-end unit tests for the cmd/cpp C preprocessor.
//
// Each test writes a source file, runs the built b6cpp binary over it, and
// checks the preprocessed output.  b6cpp is always invoked with -P so it omits
// the `# line "file"` markers, leaving output that is easy to match.  The tool
// still emits blank lines where directives were, so the checks look for the
// presence/absence of substrings rather than exact text.
//
// gtest_discover_tests runs each case in its own process; scratch files are
// named after the running test so they stay unique and survive as artefacts.
//
#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#ifndef B6CPP_PATH
#error "B6CPP_PATH (path to the b6cpp binary) must be defined by the build"
#endif

// Name of the running test, e.g. "Cpp.ObjectMacro", used to name scratch files.
static std::string current_test_name()
{
    const auto *info = ::testing::UnitTest::GetInstance()->current_test_info();
    return std::string(info->test_suite_name()) + "." + info->name();
}

// Write `text` to `path`, truncating any existing file.
static void write_file(const std::string &path, const std::string &text)
{
    std::ofstream f(path, std::ios::trunc);
    f << text;
}

// Read the whole file at `path` into a string.
static std::string read_file(const std::string &path)
{
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Preprocess `source` and return b6cpp's output.  `extra` holds any extra
// command-line options (e.g. "-DFOO=1" or "-Iincdir"); b6cpp is always run with
// -P so the output carries no line markers.
static std::string preprocess(const std::string &source, const std::string &extra = "")
{
    std::string base = current_test_name();
    std::string in   = base + ".in";
    std::string out  = base + ".out";

    write_file(in, source);
    std::string cmd = std::string(B6CPP_PATH) + " -P " + extra + " " + in + " " + out;
    EXPECT_EQ(std::system(cmd.c_str()), 0) << "b6cpp failed: " << cmd;
    return read_file(out);
}

// True if `haystack` contains `needle`.
static bool has(const std::string &haystack, const std::string &needle)
{
    return haystack.find(needle) != std::string::npos;
}

// An object-like macro is expanded to its value.
TEST(Cpp, ObjectMacro)
{
    std::string out = preprocess("#define N 42\nint x = N;\n");
    EXPECT_TRUE(has(out, "int x = 42;")) << out;
    EXPECT_FALSE(has(out, "N;")) << out;
}

// A function-like macro expands with its actual arguments substituted.
TEST(Cpp, FunctionMacro)
{
    std::string out = preprocess("#define ADD(a,b) ((a)+(b))\nADD(1,2)\n");
    EXPECT_TRUE(has(out, "((1)+(2))")) << out;
}

// Comments are stripped and their text never reaches the output.
TEST(Cpp, StripsComments)
{
    std::string out = preprocess("keep /* drop this */ more\n");
    EXPECT_TRUE(has(out, "keep")) << out;
    EXPECT_TRUE(has(out, "more")) << out;
    EXPECT_FALSE(has(out, "drop this")) << out;
    EXPECT_FALSE(has(out, "/*")) << out;
}

// #ifdef selects the taken branch and drops the other; here FOO is undefined.
TEST(Cpp, IfdefUndefined)
{
    std::string out = preprocess("#ifdef FOO\nyes\n#else\nno\n#endif\n");
    EXPECT_TRUE(has(out, "no")) << out;
    EXPECT_FALSE(has(out, "yes")) << out;
}

// #ifndef takes its branch when the macro is not defined.
TEST(Cpp, IfndefTaken)
{
    std::string out = preprocess("#ifndef BAR\nnobar\n#endif\n");
    EXPECT_TRUE(has(out, "nobar")) << out;
}

// A -D definition on the command line activates a matching #ifdef branch.
TEST(Cpp, CommandLineDefine)
{
    std::string out = preprocess("#ifdef FOO\nyes\n#else\nno\n#endif\n", "-DFOO=1");
    EXPECT_TRUE(has(out, "yes")) << out;
    EXPECT_FALSE(has(out, "no")) << out;
}

// #include pulls in a header found on the -I search path, and its macros apply.
TEST(Cpp, IncludeWithSearchPath)
{
    std::string base = current_test_name();
    std::string dir  = base + ".incdir";
    std::system(("mkdir -p " + dir).c_str());
    write_file(dir + "/hdr.h", "#define FROMHDR 99\n");

    std::string out = preprocess("#include \"hdr.h\"\nint y = FROMHDR;\n", "-I" + dir);
    EXPECT_TRUE(has(out, "int y = 99;")) << out;
}
