//
// Unit tests for the cmd/nm engine.
//
// The engine is driven through nm_run(argc, argv), which parses its own argv
// exactly like the b6nm command but returns the exit code instead of calling
// exit(), so each listing runs in-process.  Each test builds a small a.out
// object with a known symbol table -- written with the very same libaout
// encoders the assembler uses (fputhdr/fputsym) -- then captures what nm_run
// prints to stdout.
//
#include <gtest/gtest.h>

#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <string>
#include <vector>

// The engine and the shared on-disk encoders are compiled as C.
extern "C" {
#include "nm.h"

#include "besm6/b.out.h"
}

#define W 6 // BESM-6 word length in bytes

namespace {

// Name of the currently running test, e.g. "Nm.BasicListing".
std::string current_test_name()
{
    const auto *info = ::testing::UnitTest::GetInstance()->current_test_info();
    return std::string(info->test_suite_name()) + "." + info->name();
}

struct Sym {
    const char *name;
    int type;   // n_type (N_* value, optionally | N_EXT)
    long value; // n_value
};

// Write a relocatable a.out object holding exactly these symbols, terminating
// the symbol table the way the assembler does: the entries back to back,
// followed by 1..W zero pad bytes (the first of which is the zero-length entry
// that stops nm's read loop).  a_syms counts the entries plus that padding.
void build_object(const std::string &path, const std::vector<Sym> &syms)
{
    // Each entry on disk is 1 len + 1 type + 3 value + name bytes.
    long stlength = std::accumulate(syms.begin(), syms.end(), 0L,
                                    [](long acc, const Sym &s) {
                                        return acc + (long)std::strlen(s.name) + 5;
                                    });
    long pad = W - stlength % W; // 1..W, never 0 (matches cmd/as)

    struct exec hdr;
    std::memset(&hdr, 0, sizeof(hdr));
    hdr.a_magic = FMAGIC;
    hdr.a_syms  = stlength + pad;
    hdr.a_flag  = RELFLG;

    FILE *f = std::fopen(path.c_str(), "w");
    ASSERT_NE(f, nullptr) << "fopen " << path;
    fputhdr(&hdr, f);
    for (const auto &s : syms) {
        struct nlist e;
        e.n_len   = std::strlen(s.name);
        e.n_type  = s.type;
        e.n_value = s.value;
        e.n_name  = const_cast<char *>(s.name);
        fputsym(&e, f);
    }
    for (long i = 0; i < pad; i++)
        std::putc(0, f);
    std::fclose(f);
}

// Run b6nm with the given arguments and return everything it wrote to stdout.
// nm_run() may rewrite entries of argv, so a fresh vector is built every call.
std::string run_nm(std::vector<std::string> args)
{
    // s is non-const: nm_run() may rewrite argv in place, and the non-const
    // string::data() overload is what yields a writable char*.  (stlcstrReturn
    // is a false positive: the lambda returns char*, not run_nm's std::string.)
    std::vector<char *> argv(args.size());
    // cppcheck-suppress constParameterReference
    // cppcheck-suppress stlcstrReturn
    std::transform(args.begin(), args.end(), argv.begin(), [](std::string &s) { return s.data(); });
    argv.push_back(nullptr);

    testing::internal::CaptureStdout();
    nm_run((int)args.size(), argv.data());
    std::fflush(stdout);
    return testing::internal::GetCapturedStdout();
}

// Index at which `needle` appears in `hay`, or std::string::npos.
size_t at(const std::string &hay, const std::string &needle)
{
    return hay.find(needle);
}

} // namespace

// A defined object prints "value type name" per symbol, default-sorted by name.
TEST(Nm, BasicListing)
{
    std::string obj = current_test_name() + ".o";
    build_object(obj, {
                          { "main", N_TEXT | N_EXT, 010 },
                          { "data1", N_DATA, 020 },
                          { "udef", N_UNDF | N_EXT, 0 },
                      });

    std::string out = run_nm({ "b6nm", obj });

    // External text -> 'T', local data -> 'd', external undefined -> 'U'.
    EXPECT_NE(at(out, " T main"), std::string::npos) << out;
    EXPECT_NE(at(out, " d data1"), std::string::npos) << out;
    EXPECT_NE(at(out, " U udef"), std::string::npos) << out;
    // Undefined symbols print blank where the value would be.
    EXPECT_NE(at(out, "      U udef"), std::string::npos) << out;
    // Default sort is lexical: data1 < main < udef.
    EXPECT_LT(at(out, "data1"), at(out, "main"));
    EXPECT_LT(at(out, "main"), at(out, "udef"));

    unlink(obj.c_str());
}

// -g keeps only external (global) symbols.
TEST(Nm, GlobalsOnly)
{
    std::string obj = current_test_name() + ".o";
    build_object(obj, {
                          { "main", N_TEXT | N_EXT, 010 },
                          { "data1", N_DATA, 020 }, // local -> dropped by -g
                          { "udef", N_UNDF | N_EXT, 0 },
                      });

    std::string out = run_nm({ "b6nm", "-g", obj });

    EXPECT_NE(at(out, "main"), std::string::npos) << out;
    EXPECT_NE(at(out, "udef"), std::string::npos) << out;
    EXPECT_EQ(at(out, "data1"), std::string::npos) << out;

    unlink(obj.c_str());
}

// -u keeps only undefined symbols and prints just the names (no value/type).
TEST(Nm, UndefinedOnly)
{
    std::string obj = current_test_name() + ".o";
    build_object(obj, {
                          { "main", N_TEXT | N_EXT, 010 },
                          { "udef", N_UNDF | N_EXT, 0 },
                      });

    std::string out = run_nm({ "b6nm", "-u", obj });

    EXPECT_NE(at(out, "udef"), std::string::npos) << out;
    EXPECT_EQ(at(out, "main"), std::string::npos) << out;
    // With -u the type character is suppressed.
    EXPECT_EQ(at(out, " U "), std::string::npos) << out;

    unlink(obj.c_str());
}

// -n sorts by value instead of by name.
TEST(Nm, NumericSort)
{
    std::string obj = current_test_name() + ".o";
    build_object(obj, {
                          { "bbb", N_DATA, 030 },
                          { "aaa", N_DATA, 010 },
                          { "ccc", N_DATA, 020 },
                      });

    std::string out = run_nm({ "b6nm", "-n", obj });

    // Ascending by value: aaa(010) < ccc(020) < bbb(030).
    EXPECT_LT(at(out, "aaa"), at(out, "ccc")) << out;
    EXPECT_LT(at(out, "ccc"), at(out, "bbb")) << out;

    unlink(obj.c_str());
}

// -p leaves the symbols in symbol-table (insertion) order.
TEST(Nm, NoSort)
{
    std::string obj = current_test_name() + ".o";
    build_object(obj, {
                          { "zzz", N_DATA, 010 },
                          { "aaa", N_DATA, 020 },
                          { "mmm", N_DATA, 030 },
                      });

    std::string out = run_nm({ "b6nm", "-p", obj });

    // File order is preserved, not sorted.
    EXPECT_LT(at(out, "zzz"), at(out, "aaa")) << out;
    EXPECT_LT(at(out, "aaa"), at(out, "mmm")) << out;

    unlink(obj.c_str());
}
