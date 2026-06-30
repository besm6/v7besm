//
// Unit tests for the cmd/strip engine.
//
// The engine is driven through strip_run(argc, argv), which parses its own argv
// exactly like the b6strip command but returns the exit code instead of calling
// exit(), so each run happens in-process.  strip rewrites an object in place:
// it copies const+text+data after the header, clears a_syms and sets RELFLG,
// and truncates whatever followed (the symbol/relocation tables).  Each test
// writes a small object -- header via the very same libaout encoder the
// assembler uses (fputhdr), followed by filler segment bytes -- runs strip_run,
// then re-reads the header (fgethdr) and the file size to check the result.
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
#include "strip.h"

#include "besm6/b.out.h"
}

#define W 6 // BESM-6 word length in bytes

namespace {

// Name of the currently running test, e.g. "Strip.RemovesSymbols".
std::string current_test_name()
{
    const auto *info = ::testing::UnitTest::GetInstance()->current_test_info();
    return std::string(info->test_suite_name()) + "." + info->name();
}

// Write an a.out object: a header carrying the given segment/symbol sizes and
// flag, followed by a_const+a_text+a_data+a_syms filler bytes (strip copies the
// const+text+data bytes after the header, so they must exist on disk).
void build_object(const std::string &path, long a_const, long a_text, long a_data, long a_syms,
                  long flag)
{
    struct exec hdr;
    std::memset(&hdr, 0, sizeof(hdr));
    hdr.a_magic = FMAGIC;
    hdr.a_const = a_const;
    hdr.a_text  = a_text;
    hdr.a_data  = a_data;
    hdr.a_syms  = a_syms;
    hdr.a_flag  = flag;

    FILE *f = std::fopen(path.c_str(), "w");
    ASSERT_NE(f, nullptr) << "fopen " << path;
    fputhdr(&hdr, f);
    for (long i = 0; i < a_const + a_text + a_data + a_syms; i++)
        std::fputc('x', f);
    std::fclose(f);
}

// Read back the header of an object file.
struct exec read_header(const std::string &path)
{
    struct exec hdr;
    std::memset(&hdr, 0, sizeof(hdr));
    FILE *f = std::fopen(path.c_str(), "r");
    EXPECT_NE(f, nullptr) << "fopen " << path;
    if (f) {
        fgethdr(f, &hdr);
        std::fclose(f);
    }
    return hdr;
}

// Size of a file in bytes.
long file_size(const std::string &path)
{
    FILE *f = std::fopen(path.c_str(), "r");
    EXPECT_NE(f, nullptr) << "fopen " << path;
    if (!f)
        return -1;
    std::fseek(f, 0L, SEEK_END);
    long n = std::ftell(f);
    std::fclose(f);
    return n;
}

struct Result {
    int rc;          // strip_run() exit code
    std::string out; // everything written to stdout
};

// Run b6strip with the given arguments, returning its exit code and stdout.
Result run_strip(std::vector<std::string> args)
{
    // s is non-const: strip_run() may rewrite argv in place, and the non-const
    // string::data() overload is what yields a writable char*.
    std::vector<char *> argv(args.size());
    // cppcheck-suppress constParameterReference
    // cppcheck-suppress stlcstrReturn
    std::transform(args.begin(), args.end(), argv.begin(), [](std::string &s) { return s.data(); });
    argv.push_back(nullptr);

    testing::internal::CaptureStdout();
    int rc = strip_run((int)args.size(), argv.data());
    std::fflush(stdout);
    return { rc, testing::internal::GetCapturedStdout() };
}

} // namespace

// Stripping an object with a symbol table clears a_syms, sets RELFLG, and
// truncates the file to the header plus the const+text+data segments.
TEST(Strip, RemovesSymbols)
{
    std::string obj = current_test_name() + ".o";
    build_object(obj, 2 * W, 3 * W, 4 * W, 5 * W, RELFLG); // syms present

    Result r = run_strip({ "b6strip", obj });

    EXPECT_EQ(r.rc, 0) << r.out;
    struct exec hdr = read_header(obj);
    EXPECT_EQ((long)hdr.a_syms, 0L) << r.out;
    EXPECT_TRUE(hdr.a_flag & RELFLG) << r.out;
    EXPECT_EQ(file_size(obj), (long)(HDRSZ + 2 * W + 3 * W + 4 * W)) << r.out;

    unlink(obj.c_str());
}

// An object that already has no symbols and is marked relocatable is reported
// as already stripped and left alone.
TEST(Strip, AlreadyStripped)
{
    std::string obj = current_test_name() + ".o";
    build_object(obj, W, W, W, 0, RELFLG); // no syms, already RELFLG
    long before = file_size(obj);

    Result r = run_strip({ "b6strip", obj });

    EXPECT_EQ(r.rc, 0) << r.out;
    EXPECT_NE(r.out.find("already stripped"), std::string::npos) << r.out;
    EXPECT_EQ(file_size(obj), before) << r.out;

    unlink(obj.c_str());
}

// A file that is not in a.out format is rejected with a non-zero exit code.
TEST(Strip, NotAnObject)
{
    std::string obj = current_test_name() + ".o";
    FILE *f = std::fopen(obj.c_str(), "w");
    ASSERT_NE(f, nullptr) << "fopen " << obj;
    std::fputs("not an object file, just some bytes\n", f);
    std::fclose(f);

    Result r = run_strip({ "b6strip", obj });

    EXPECT_NE(r.rc, 0) << r.out;
    EXPECT_NE(r.out.find("not in a.out format"), std::string::npos) << r.out;

    unlink(obj.c_str());
}
