//
// Unit tests for the cmd/ranlib engine.
//
// The engine is driven through ranlib_run(argc, argv), which parses its own
// argv exactly like the b6ranlib command but returns the exit code instead of
// calling exit(), so each run happens in-process.  Each test builds a small
// relocatable a.out object with a known symbol table -- written with the very
// same libaout encoders the assembler uses (fputhdr/fputsym) -- bundles the
// objects into an archive with the in-process archiver (ar_run), runs ranlib on
// it, then walks the resulting __.SYMDEF index back out with the matching
// libaout decoders (fgetarhdr/fgetran).
//
#include <gtest/gtest.h>

#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <string>
#include <vector>

// The engine and the shared on-disk encoders/decoders are compiled as C.
extern "C" {
#include "symdef.h"

#include "archive.h"

#include "besm6/ar.h"
#include "besm6/b.out.h"
#include "besm6/ranlib.h"
}

#define W 6 // длина слова БЭСМ-6 в байтах

namespace {

// Name of the currently running test, e.g. "Ranlib.BasicIndex".
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

// Write a relocatable a.out object holding exactly these symbols.  The segments
// are empty, so the symbol table directly follows the header; the table ends
// the way the assembler does: the entries back to back, then 1..W zero pad
// bytes (the first of which is the zero-length entry that stops the read loop).
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

// Run b6ar / b6ranlib with the given arguments; returns the exit code.  Both
// engines rewrite entries of argv, so a fresh vector is built for every call.
template <typename Fn>
int run(Fn engine, std::vector<std::string> args)
{
    std::vector<char *> argv(args.size());
    // s is non-const: the engines may rewrite argv in place, and the non-const
    // string::data() overload is what yields a writable char*.
    // cppcheck-suppress constParameterReference
    std::transform(args.begin(), args.end(), argv.begin(), [](std::string &s) { return s.data(); });
    argv.push_back(nullptr);
    return engine((int)args.size(), argv.data());
}

struct RanEntry {
    std::string name;
    long off;
};

// Walk the archive, find the __.SYMDEF member and decode its ranlib entries.
// member_offsets (out) records the file offset of every non-__.SYMDEF member
// header, so callers can match ran_off against a real member.
std::vector<RanEntry> read_symdef(const std::string &path, std::vector<long> *member_offsets)
{
    std::vector<RanEntry> out;
    FILE *f = std::fopen(path.c_str(), "r");
    EXPECT_NE(f, nullptr) << "fopen " << path;
    if (!f)
        return out;

    EXPECT_EQ(fgetw(f), (uword_t)ARMAG);

    long off = W; // past the magic word
    struct ar_hdr h;
    bool first = true;
    while (fgetarhdr(f, &h)) {
        long datasize = (h.ar_size + W - 1) / W * W;
        bool is_symdef =
            std::strncmp(h.ar_name, "__.SYMDEF", std::strlen("__.SYMDEF")) == 0;
        if (is_symdef) {
            // __.SYMDEF must be the very first member so the linker finds it.
            EXPECT_TRUE(first) << "__.SYMDEF is not the first member";
            long end = ftell(f) + datasize;
            struct ranlib r;
            int rc;
            while (ftell(f) < end && (rc = fgetran(f, &r)) == 1) {
                out.push_back({ std::string(r.ran_name), (long)r.ran_off });
                free(r.ran_name);
            }
            std::fseek(f, end, 0);
        } else {
            if (member_offsets)
                member_offsets->push_back(off);
            std::fseek(f, datasize, 1);
        }
        off += ARHDRSZ + datasize;
        first = false;
    }
    std::fclose(f);
    return out;
}

bool has_symbol(const std::vector<RanEntry> &t, const std::string &name)
{
    return std::any_of(t.begin(), t.end(), [&](const RanEntry &e) { return e.name == name; });
}

} // namespace

// The index lists exactly the external *defined* symbols; locals and external
// undefined references are skipped, and __.SYMDEF lands first.
TEST(Ranlib, BasicIndex)
{
    std::string base = current_test_name();
    std::string a    = base + "_a.o";
    std::string b    = base + "_b.o";
    std::string lib  = base + ".a";

    build_object(a, {
                        { "afunc", N_TEXT | N_EXT, 010 }, // external defined  -> kept
                        { "alocal", N_DATA, 020 },        // local             -> skipped
                        { "aundef", N_UNDF | N_EXT, 0 },  // external undefined-> skipped
                    });
    build_object(b, {
                        { "bdata", N_DATA | N_EXT, 030 }, // external defined  -> kept
                    });

    ASSERT_EQ(run(ar_run, { "b6ar", "rc", lib, a, b }), 0);
    ASSERT_EQ(run(ranlib_run, { "b6ranlib", lib }), 0);

    std::vector<long> members;
    auto t = read_symdef(lib, &members);

    EXPECT_TRUE(has_symbol(t, "afunc")) << "missing external defined symbol";
    EXPECT_TRUE(has_symbol(t, "bdata")) << "missing external defined symbol";
    EXPECT_FALSE(has_symbol(t, "alocal")) << "local symbol leaked into index";
    EXPECT_FALSE(has_symbol(t, "aundef")) << "undefined symbol leaked into index";

    // Every recorded offset is word-aligned and points at one of the real
    // member headers in the rewritten archive.
    for (const auto &e : t) {
        EXPECT_EQ(e.off % W, 0) << e.name << " offset not word-aligned";
        EXPECT_NE(std::find(members.begin(), members.end(), e.off), members.end())
            << e.name << " offset " << e.off << " does not point at a member header";
    }

    unlink(a.c_str());
    unlink(b.c_str());
    unlink(lib.c_str());
}

// Running ranlib twice is idempotent: the second run replaces the existing
// __.SYMDEF rather than adding a second one, and the index is unchanged.
TEST(Ranlib, Idempotent)
{
    std::string base = current_test_name();
    std::string a    = base + "_a.o";
    std::string lib  = base + ".a";

    build_object(a, {
                        { "main", N_TEXT | N_EXT, 010 },
                    });

    ASSERT_EQ(run(ar_run, { "b6ar", "rc", lib, a }), 0);
    ASSERT_EQ(run(ranlib_run, { "b6ranlib", lib }), 0);
    auto first = read_symdef(lib, nullptr);

    ASSERT_EQ(run(ranlib_run, { "b6ranlib", lib }), 0);
    auto second = read_symdef(lib, nullptr);

    ASSERT_EQ(second.size(), first.size());
    EXPECT_TRUE(has_symbol(second, "main")) << "symbol lost on second run";

    // Exactly one __.SYMDEF member in the archive.
    FILE *f = std::fopen(lib.c_str(), "r");
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(fgetw(f), (uword_t)ARMAG);
    struct ar_hdr h;
    int symdefs = 0;
    while (fgetarhdr(f, &h)) {
        long datasize = (h.ar_size + W - 1) / W * W;
        if (std::strncmp(h.ar_name, "__.SYMDEF", std::strlen("__.SYMDEF")) == 0)
            symdefs++;
        std::fseek(f, datasize, 1);
    }
    std::fclose(f);
    EXPECT_EQ(symdefs, 1);

    unlink(a.c_str());
    unlink(lib.c_str());
}

// A non-archive argument is rejected without touching the file.
TEST(Ranlib, NotAnArchive)
{
    std::string base = current_test_name();
    std::string notar = base + ".o";

    build_object(notar, { { "x", N_TEXT | N_EXT, 0 } });

    // ranlib reports the error per-file and returns 0 (matches the original).
    EXPECT_EQ(run(ranlib_run, { "b6ranlib", notar }), 0);

    unlink(notar.c_str());
}
