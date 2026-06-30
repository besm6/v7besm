//
// Unit tests for the cmd/size engine.
//
// The engine is driven through size_run(argc, argv), which parses its own argv
// exactly like the b6size command but returns the exit code instead of calling
// exit(), so each listing runs in-process.  size only reads the a.out header,
// so each test writes a small object with chosen segment sizes -- using the very
// same libaout encoder the assembler uses (fputhdr) -- then captures what
// size_run prints to stdout.
//
#include <gtest/gtest.h>

#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// The engine and the shared on-disk encoders are compiled as C.
extern "C" {
#include "size.h"

#include "besm6/b.out.h"
}

#define W 6 // длина слова БЭСМ-6 в байтах

namespace {

// Name of the currently running test, e.g. "Size.ByteSizes".
std::string current_test_name()
{
    const auto *info = ::testing::UnitTest::GetInstance()->current_test_info();
    return std::string(info->test_suite_name()) + "." + info->name();
}

// Write a relocatable a.out object whose header carries exactly these segment
// sizes (in bytes).  size reads only the header, so no segment images follow.
void build_object(const std::string &path, long a_const, long a_text, long a_data, long a_bss,
                  long a_abss)
{
    struct exec hdr;
    std::memset(&hdr, 0, sizeof(hdr));
    hdr.a_magic = FMAGIC;
    hdr.a_const = a_const;
    hdr.a_text  = a_text;
    hdr.a_data  = a_data;
    hdr.a_bss   = a_bss;
    hdr.a_abss  = a_abss;
    hdr.a_flag  = RELFLG;

    FILE *f = std::fopen(path.c_str(), "w");
    ASSERT_NE(f, nullptr) << "fopen " << path;
    fputhdr(&hdr, f);
    std::fclose(f);
}

struct Result {
    int rc;          // size_run() exit code
    std::string out; // everything written to stdout
};

// Run b6size with the given arguments, returning its exit code and stdout.
// size_run() may rewrite entries of argv, so a fresh vector is built every call.
Result run_size(std::vector<std::string> args)
{
    // s is non-const: size_run() may rewrite argv in place, and the non-const
    // string::data() overload is what yields a writable char*.
    std::vector<char *> argv(args.size());
    // cppcheck-suppress constParameterReference
    // cppcheck-suppress stlcstrReturn
    std::transform(args.begin(), args.end(), argv.begin(), [](std::string &s) { return s.data(); });
    argv.push_back(nullptr);

    testing::internal::CaptureStdout();
    int rc = size_run((int)args.size(), argv.data());
    std::fflush(stdout);
    return { rc, testing::internal::GetCapturedStdout() };
}

// Index at which `needle` appears in `hay`, or std::string::npos.
size_t at(const std::string &hay, const std::string &needle)
{
    return hay.find(needle);
}

} // namespace

// Byte mode prints the column header plus a tab-separated row with the five
// segment sizes, their decimal sum, and that sum in hex.
TEST(Size, ByteSizes)
{
    std::string obj = current_test_name() + ".o";
    build_object(obj, 12, 24, 36, 48, 60); // sum = 180 = 0xb4

    Result r = run_size({ "b6size", obj });

    EXPECT_EQ(r.rc, 0) << r.out;
    EXPECT_NE(at(r.out, "const\ttext\tdata\tbss\tabss\tdec\thex\n"), std::string::npos) << r.out;
    EXPECT_NE(at(r.out, "12\t24\t36\t48\t60\t180\tb4\t" + obj + "\n"), std::string::npos) << r.out;

    unlink(obj.c_str());
}

// -w divides each size by the word length (6) and reports the row in words.
TEST(Size, WordSizes)
{
    std::string obj = current_test_name() + ".o";
    build_object(obj, 12, 24, 36, 48, 60); // /6 -> 2 4 6 8 10, sum 30 = 0x1e

    Result r = run_size({ "b6size", "-w", obj });

    EXPECT_EQ(r.rc, 0) << r.out;
    EXPECT_NE(at(r.out, "2\t4\t6\t8\t10\t30\t1e\t" + obj + "\n"), std::string::npos) << r.out;

    unlink(obj.c_str());
}

// The column header is printed once, no matter how many objects are listed.
TEST(Size, HeaderOnce)
{
    std::string a = current_test_name() + ".a.o";
    std::string b = current_test_name() + ".b.o";
    build_object(a, W, W, W, 0, 0);
    build_object(b, W, W, W, 0, 0);

    Result r = run_size({ "b6size", a, b });

    EXPECT_EQ(r.rc, 0) << r.out;
    // Header appears exactly once across the two rows.
    EXPECT_NE(at(r.out, "const"), std::string::npos) << r.out;
    EXPECT_EQ(r.out.find("const", at(r.out, "const") + 1), std::string::npos) << r.out;

    unlink(a.c_str());
    unlink(b.c_str());
}

// An unknown flag is rejected with a non-zero exit code.
TEST(Size, BadFlag)
{
    std::string obj = current_test_name() + ".o";
    build_object(obj, W, W, W, 0, 0);

    Result r = run_size({ "b6size", "-x", obj });

    EXPECT_NE(r.rc, 0) << r.out;

    unlink(obj.c_str());
}
