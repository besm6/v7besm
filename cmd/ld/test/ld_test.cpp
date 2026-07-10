//
// Unit test for the cmd/ld linker engine.
//
// End-to-end: assemble a tiny program with the b6as binary, link the resulting
// object in-process through ld_link(), then read the linked image as a raw byte
// array and check it word by word.  The program is a single long-address jump
// to a text label; linking it at a non-default base relocates the address and
// must preserve the instruction's opcode -- the regression check for LD-2 (the
// 15-bit long-address mask in relocate_halfword()): a 20-bit mask would clear the opcode
// bits of the long instruction while relocating.
//
// The ld engine carries scattered, non-reset globals, so the suite performs a
// single link per process (one TEST); gtest_discover_tests runs each test case
// in its own process anyway.
//
#include <gtest/gtest.h>
#include <sys/stat.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// The engine library is compiled as C; ld.h is C++-safe.
extern "C" {
#include "ld.h"
}

#ifndef B6AS_PATH
#error "B6AS_PATH (path to the b6as binary) must be defined by the build"
#endif

#ifndef B6AR_PATH
#error "B6AR_PATH (path to the b6ar binary) must be defined by the build"
#endif

// High 24-bit half-word at word index `w` (a BESM-6 word is two big-endian
// half-words; the high/left half-word executes first).  Same helpers as
// cmd/as/test/as_test.cpp.
static long word_high(const std::vector<unsigned char> &b, int w)
{
    size_t o = (size_t)w * 6;
    return ((long)b[o] << 16) | ((long)b[o + 1] << 8) | b[o + 2];
}

// Low 24-bit half-word at word index `w` (header fields live in the low half).
static long word_low(const std::vector<unsigned char> &b, int w)
{
    size_t o = (size_t)w * 6 + 3;
    return ((long)b[o] << 16) | ((long)b[o + 1] << 8) | b[o + 2];
}

// Read a whole file into a byte vector.
static std::vector<unsigned char> read_file(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(f)),
                                      std::istreambuf_iterator<char>());
}

// Name of the running test, e.g. "Link.AssembleAndLink", used to name the
// scratch files so they are unique and survive as debugging artefacts.
static std::string current_test_name()
{
    const auto *info = ::testing::UnitTest::GetInstance()->current_test_info();
    return std::string(info->test_suite_name()) + "." + info->name();
}

// Assemble a small program, link it at base 0100, and verify the linked image.
TEST(Link, AssembleAndLink)
{
    std::string base   = current_test_name();
    std::string sfile  = base + ".s";
    std::string ofile  = base + ".o";
    std::string image  = base + ".out";

    // A long jump to a text label: one short instr (atx, high half) and one long
    // instr (uj, low half) pack into text word 9.  The assembler emits `uj foo`
    // as 03000000|9 with an RTEXT relocation (long field, see cmd/as Assemble.Globl).
    {
        std::ofstream src(sfile, std::ios::trunc);
        src << "        .globl foo\n"
               "foo:    atx 0\n"
               "        uj foo\n";
    }

    // Assemble with the b6as binary -> object file.
    std::string ascmd = std::string(B6AS_PATH) + " " + sfile + " -o " + ofile;
    ASSERT_EQ(std::system(ascmd.c_str()), 0) << "assembler failed: " << ascmd;

    // Link in-process at base address 0100 (so foo relocates 9 -> 0100).
    char a0[] = "ld";
    char a1[] = "-o";
    std::vector<char> a2(image.begin(), image.end());
    a2.push_back('\0');
    char a3[] = "-T0100";
    std::vector<char> a4(ofile.begin(), ofile.end());
    a4.push_back('\0');
    char *argv[] = { a0, a1, a2.data(), a3, a4.data(), nullptr };
    EXPECT_EQ(ld_link(5, argv), 0);

    auto img = read_file(image);
    ASSERT_GE(img.size(), (size_t)9 * 6); // header (8 words) + at least one text word

    // Header: 8 words, each value in the low half-word (a_magic is a full word).
    EXPECT_EQ((word_high(img, 0) << 24) | word_low(img, 0), 02044252323200407L); // a_magic = FMAGIC
    EXPECT_EQ(word_low(img, 1), 0L);    // a_const: no const segment
    EXPECT_EQ(word_low(img, 2), 6L);    // a_text:  one 48-bit word
    EXPECT_EQ(word_low(img, 3), 0L);    // a_data
    EXPECT_EQ(word_low(img, 4), 0L);    // a_bss
    EXPECT_EQ(word_low(img, 6), 0100L); // a_entry = torigin = load base 0100
    EXPECT_EQ(word_low(img, 7), 1L);    // a_flag = RELFLG (fully linked, rflag==0)

    // The relocated text word (file word 8): atx 0 in the high half (unchanged),
    // `uj foo` in the low half with foo relocated to 0100.  The opcode bits
    // (03000000) MUST survive relocation -- this is the LD-2 check.
    EXPECT_EQ(word_high(img, 8), 0L);          // atx 0
    EXPECT_EQ(word_low(img, 8), 03000000L | 0100L); // uj foo -> 0100, opcode preserved
}

// Assemble a tiny object, archive it as libfoo.a inside a sub-directory, then
// link "-u foo -L <subdir> -lfoo": the -L search path must locate the archive
// (which lives nowhere on the default, empty path) and pull in the member that
// defines foo.
TEST(Link, LibrarySearchPath)
{
    std::string base   = current_test_name();
    std::string sfile  = base + ".s";
    std::string ofile  = base + ".o";
    std::string image  = base + ".out";
    std::string libdir = base + ".libdir";
    std::string archive = libdir + "/libfoo.a";

    {
        std::ofstream src(sfile, std::ios::trunc);
        src << "        .globl foo\n"
               "foo:    atx 0\n"
               "        uj foo\n";
    }
    std::string ascmd = std::string(B6AS_PATH) + " " + sfile + " -o " + ofile;
    ASSERT_EQ(std::system(ascmd.c_str()), 0) << "assembler failed: " << ascmd;

    // Put the archive in its own directory, reachable only via -L.
    ASSERT_EQ(mkdir(libdir.c_str(), 0777) == 0 || errno == EEXIST, true);
    std::string arcmd = std::string(B6AR_PATH) + " r " + archive + " " + ofile;
    ASSERT_EQ(std::system(arcmd.c_str()), 0) << "archiver failed: " << arcmd;

    // Link: -u foo forces the reference, -L points at the archive's directory.
    char a0[] = "ld";
    char a1[] = "-u";
    char a2[] = "foo";
    char a3[] = "-L";
    std::vector<char> a4(libdir.begin(), libdir.end());
    a4.push_back('\0');
    char a5[] = "-lfoo";
    char a6[] = "-o";
    std::vector<char> a7(image.begin(), image.end());
    a7.push_back('\0');
    char *argv[] = { a0, a1, a2, a3, a4.data(), a5, a6, a7.data(), nullptr };
    EXPECT_EQ(ld_link(8, argv), 0);

    auto img = read_file(image);
    ASSERT_GE(img.size(), (size_t)9 * 6); // header + the pulled-in member's text
    EXPECT_EQ(word_low(img, 2), 6L);      // a_text: foo's one word was linked in
}

// Same as LibrarySearchPath, but the directory is glued to the flag ("-L<dir>")
// instead of passed as a separate argument.  This exercises the glued-form
// parsing in both pass 1 and pass 2.
TEST(Link, LibrarySearchPathGlued)
{
    std::string base   = current_test_name();
    std::string sfile  = base + ".s";
    std::string ofile  = base + ".o";
    std::string image  = base + ".out";
    std::string libdir = base + ".libdir";
    std::string archive = libdir + "/libfoo.a";

    {
        std::ofstream src(sfile, std::ios::trunc);
        src << "        .globl foo\n"
               "foo:    atx 0\n"
               "        uj foo\n";
    }
    std::string ascmd = std::string(B6AS_PATH) + " " + sfile + " -o " + ofile;
    ASSERT_EQ(std::system(ascmd.c_str()), 0) << "assembler failed: " << ascmd;

    ASSERT_EQ(mkdir(libdir.c_str(), 0777) == 0 || errno == EEXIST, true);
    std::string arcmd = std::string(B6AR_PATH) + " r " + archive + " " + ofile;
    ASSERT_EQ(std::system(arcmd.c_str()), 0) << "archiver failed: " << arcmd;

    // Link: the directory is glued to -L (e.g. "-LLink.....libdir").
    char a0[] = "ld";
    char a1[] = "-u";
    char a2[] = "foo";
    std::string ldir = "-L" + libdir;
    std::vector<char> a3(ldir.begin(), ldir.end());
    a3.push_back('\0');
    char a4[] = "-lfoo";
    char a5[] = "-o";
    std::vector<char> a6(image.begin(), image.end());
    a6.push_back('\0');
    char *argv[] = { a0, a1, a2, a3.data(), a4, a5, a6.data(), nullptr };
    EXPECT_EQ(ld_link(7, argv), 0);

    auto img = read_file(image);
    ASSERT_GE(img.size(), (size_t)9 * 6);
    EXPECT_EQ(word_low(img, 2), 6L); // a_text: foo's one word was linked in
}

// With no -L, the search path is empty, so "-lfoo" cannot be found and the
// linker aborts with "cannot open" (fatal error -> exit code 4).
TEST(LinkDeath, EmptyLibrarySearchPath)
{
    char a0[] = "ld";
    char a1[] = "-lfoo";
    char *argv[] = { a0, a1, nullptr };
    EXPECT_EXIT(ld_link(2, argv), ::testing::ExitedWithCode(4), "");
}

// Write `text` to `path` and assemble it into `path`.o with the b6as binary.
static void assemble_to(const std::string &sfile, const std::string &ofile, const char *text)
{
    {
        std::ofstream src(sfile, std::ios::trunc);
        src << text;
    }
    std::string cmd = std::string(B6AS_PATH) + " " + sfile + " -o " + ofile;
    ASSERT_EQ(std::system(cmd.c_str()), 0) << "assembler failed: " << cmd;
}

// Run "ld -o image obj..." through the in-process engine and return its errlev.
// ld_link() takes a writable argv, so hand it pointers into `names`, which must
// outlive the call and must not reallocate once they are taken.
static int link_files(const std::string &image, const std::vector<std::string> &objs)
{
    static char a0[] = "ld";
    static char a1[] = "-o";
    std::vector<std::string> names{ image };
    names.insert(names.end(), objs.begin(), objs.end());

    std::vector<char *> argv{ a0, a1 };
    std::transform(names.begin(), names.end(), std::back_inserter(argv),
                   [](std::string &s) { return &s[0]; });
    argv.push_back(nullptr);
    return ld_link((int)argv.size() - 1, argv.data());
}

// One symbol-table entry as serialized by cmd/libaout/fputsym.c.
struct Sym {
    std::string name;
    long type;
    long value;
};

// Walk a linked image's symbol table.  Layout is header (48 B), the const/text/
// data segments, then -- only while the file is still relocatable, i.e. RELFLG
// (a_flag bit 0) is clear -- their relocation records, and finally the symbol
// table.  Each entry is n_len(1) n_type(1) n_value(3, big-endian) followed by
// n_len name bytes.  Cf. read_symbols() in cmd/as/test/as_test.cpp, which always
// sees relocation records because it reads objects rather than executables.
static std::vector<Sym> read_symbols(const std::vector<unsigned char> &b)
{
    long segs   = word_low(b, 1) + word_low(b, 2) + word_low(b, 3);
    long a_syms = word_low(b, 5);
    size_t off  = 48 + (size_t)((word_low(b, 7) & 1) ? segs : 2 * segs);
    size_t end  = off + (size_t)a_syms;
    std::vector<Sym> out;
    while (off + 5 <= end && b[off] != 0) {
        int nlen   = b[off];
        long type  = b[off + 1];
        long value = ((long)b[off + 2] << 16) | ((long)b[off + 3] << 8) | b[off + 4];
        out.push_back(
            { std::string(reinterpret_cast<const char *>(&b[off + 5]), nlen), type, value });
        off += 5 + nlen;
    }
    return out;
}

// The symbol named `name`.  Returned by value so a caller may pass a temporary
// read_symbols() result straight in.  Fails the calling test if absent.
static Sym find_symbol(const std::vector<Sym> &syms, const char *name)
{
    auto it =
        std::find_if(syms.begin(), syms.end(), [name](const Sym &s) { return s.name == name; });
    if (it == syms.end()) {
        ADD_FAILURE() << "symbol '" << name << "' is missing from the linked image";
        return {};
    }
    return *it;
}

// Two objects each interning the same literal: the words are anonymous (marked
// RMERGE by the "#expr" operator), so the linker stores the value once and points
// both references at the surviving copy.
TEST(Link, MergesIdenticalLiterals)
{
    std::string base = current_test_name();
    std::string o1 = base + ".1.o", o2 = base + ".2.o", image = base + ".out";

    assemble_to(base + ".1.s", o1, "        .globl f\nf:      xta #0x123456789a\n        uj f\n");
    assemble_to(base + ".2.s", o2, "        .globl g\ng:      xta #0x123456789a\n        uj g\n");

    char a0[] = "ld";
    char a1[] = "-o";
    std::vector<char> a2(image.begin(), image.end()), a3(o1.begin(), o1.end()),
        a4(o2.begin(), o2.end());
    a2.push_back('\0');
    a3.push_back('\0');
    a4.push_back('\0');
    char *argv[] = { a0, a1, a2.data(), a3.data(), a4.data(), nullptr };
    EXPECT_EQ(ld_link(5, argv), 0);

    auto img = read_file(image);
    EXPECT_EQ(word_low(img, 1), 6L) << "a_const: the shared literal is stored once";
    EXPECT_EQ(word_high(img, 8), 0x1234L);   // the merged word, high 24 bits
    EXPECT_EQ(word_low(img, 8), 0x56789aL);  //                 low 24 bits
}

// Words placed by ".const" are ordered data, not anonymous literals, so the
// linker must keep every one of them even when two are identical.  Merging the
// third word onto the first would leave `tbl` a two-word array -- the regression
// this whole marking scheme exists to prevent.
TEST(Link, PreservesConstDataAdjacency)
{
    std::string base = current_test_name();
    std::string ofile = base + ".o", image = base + ".out";

    assemble_to(base + ".s", ofile,
                "        .const\n"
                "        .globl tbl\n"
                "tbl:    .word 1, 2, 1\n"
                "        .text\n"
                "        .globl f\n"
                "f:      uj f\n");

    char a0[] = "ld";
    char a1[] = "-o";
    std::vector<char> a2(image.begin(), image.end()), a3(ofile.begin(), ofile.end());
    a2.push_back('\0');
    a3.push_back('\0');
    char *argv[] = { a0, a1, a2.data(), a3.data(), nullptr };
    EXPECT_EQ(ld_link(4, argv), 0);

    auto img = read_file(image);
    EXPECT_EQ(word_low(img, 1), 3 * 6L) << "a_const: all three words survive";
    EXPECT_EQ(word_low(img, 8), 1L);  // tbl[0]
    EXPECT_EQ(word_low(img, 9), 2L);  // tbl[1] -- would be tbl[2] if the 1s merged
    EXPECT_EQ(word_low(img, 10), 1L); // tbl[2]
    EXPECT_EQ(word_low(img, 6), 8L + 3); // a_entry = torigin, past the const segment
}

// A global label in ".const" is relocated through newindex[], indexed off
// ld.cindex -- the base of this file's const words.  Pass 1 used to read that map
// past the end of the file's own block (load_constants() had already stepped
// ld.cindex over it), so it recorded a bogus value, pass 2 recomputed the right
// one, and the mismatch surfaced as "name 'tbl' redefined".  Offset 0 escaped
// only because the slot it wrongly read happened to hold 0 as well.
TEST(Link, GlobalConstLabelAtNonzeroOffset)
{
    std::string base  = current_test_name();
    std::string ofile = base + ".o", image = base + ".out";

    assemble_to(base + ".s", ofile,
                "        .const\n"
                "        .word 111\n"
                "        .globl tbl\n"
                "tbl:    .word 222\n"
                "        .text\n"
                "        .globl f\n"
                "f:      uj f\n");

    ASSERT_EQ(link_files(image, { ofile }), 0);

    auto img = read_file(image);
    Sym tbl  = find_symbol(read_symbols(img), "tbl");
    EXPECT_EQ(tbl.type, 040L + 02L); // N_EXT + N_CONST
    EXPECT_EQ(tbl.value, 8L + 1);    // corigin + word offset 1
}

// The same lookup, with merging actually in play: file 1 contributes one pooled
// literal, so file 2's ".const" block starts at pooled index 1 and `tbl` must
// follow the literal rather than land on top of it.
TEST(Link, ConstLabelAfterMergedLiteral)
{
    std::string base = current_test_name();
    std::string o1 = base + ".1.o", o2 = base + ".2.o", image = base + ".out";

    assemble_to(base + ".1.s", o1, "        .globl f\nf:      xta #7\n        uj f\n");
    assemble_to(base + ".2.s", o2,
                "        .const\n"
                "        .globl tbl\n"
                "tbl:    .word 222\n"
                "        .word 333\n"
                "        .text\n"
                "        .globl g\n"
                "g:      uj g\n");

    ASSERT_EQ(link_files(image, { o1, o2 }), 0);

    auto img = read_file(image);
    EXPECT_EQ(word_low(img, 1), 3 * 6L) << "a_const: literal 7, then tbl[0..1]";
    EXPECT_EQ(word_low(img, 8), 7L);    // the pooled literal
    EXPECT_EQ(word_low(img, 9), 222L);  // tbl[0]
    EXPECT_EQ(word_low(img, 10), 333L); // tbl[1]

    Sym tbl = find_symbol(read_symbols(img), "tbl");
    EXPECT_EQ(tbl.value, 8L + 1); // corigin + pooled index 1
}

// Merging must repoint every reference, not just shrink the segment: file 2's
// "#2" has to end up addressing file 1's copy.  Both files number that literal
// differently (index 1 in file 1, index 0 in file 2), so this is what catches a
// wrong ld.cindex base in relocate_halfword().
TEST(Link, MergedLiteralIsAddressedFromEveryFile)
{
    std::string base = current_test_name();
    std::string o1 = base + ".1.o", o2 = base + ".2.o", image = base + ".out";

    assemble_to(base + ".1.s", o1,
                "        .globl f\nf:      xta #1\n        xta #2\n        uj f\n");
    assemble_to(base + ".2.s", o2,
                "        .globl g\ng:      xta #2\n        xta #3\n        uj g\n");

    ASSERT_EQ(link_files(image, { o1, o2 }), 0);

    auto img = read_file(image);
    EXPECT_EQ(word_low(img, 1), 3 * 6L) << "a_const: 1, 2, 3 -- the shared 2 stored once";
    EXPECT_EQ(word_low(img, 8), 1L);
    EXPECT_EQ(word_low(img, 9), 2L);
    EXPECT_EQ(word_low(img, 10), 3L);

    // corigin 8 + three const words = torigin 11; file 1 takes text words 11..12,
    // so file 2's two "xta" instructions pack into word 13.
    EXPECT_EQ(word_high(img, 13) & 07777, 9L) << "file 2's #2 -> file 1's word";
    EXPECT_EQ(word_low(img, 13) & 07777, 10L) << "file 2's #3 -> its own word";
}

// An unmarked ".const" word may serve as the survivor of a merge -- it does not
// move, so nothing that depends on its position is disturbed -- even though it
// can never itself be merged away.
TEST(Link, LiteralMergesOntoConstDataWord)
{
    std::string base = current_test_name();
    std::string o1 = base + ".1.o", o2 = base + ".2.o", image = base + ".out";

    assemble_to(base + ".1.s", o1,
                "        .const\n"
                "        .globl tbl\n"
                "tbl:    .word 5\n"
                "        .text\n"
                "        .globl f\n"
                "f:      uj f\n");
    assemble_to(base + ".2.s", o2, "        .globl g\ng:      xta #5\n        uj g\n");

    ASSERT_EQ(link_files(image, { o1, o2 }), 0);

    auto img = read_file(image);
    EXPECT_EQ(word_low(img, 1), 6L) << "a_const: the literal reuses tbl's word";
    EXPECT_EQ(word_low(img, 8), 5L);

    Sym tbl = find_symbol(read_symbols(img), "tbl");
    EXPECT_EQ(tbl.value, 8L); // corigin

    // corigin 8 + one const word = torigin 9; file 1 owns word 9, file 2 word 10.
    EXPECT_EQ(word_high(img, 10) & 07777, 8L) << "file 2's #5 -> tbl's word";
}

// The five boundary symbols the linker defines itself.  They carry no leading
// underscore -- the BESM-6 C compiler emits C names verbatim, so `extern int end`
// in C must resolve against a symbol spelled exactly "end" (cf. kernel/dev/mem.c).
//
// Naming each of them in a ".globl" without defining it leaves it undefined in the
// object; assign_addresses() must then define all five, and must not treat those
// dangling references as grounds for forcing -r on (were it to, the image would
// keep its relocation records and read_symbols() would look at the wrong offset).
TEST(Link, DefinesBoundarySymbols)
{
    std::string base = current_test_name();
    std::string ofile = base + ".o", image = base + ".out";

    // Give every segment a distinct, known size so the five addresses differ:
    // 2 const words, 1 text word, 1 data word, 3 bss words.
    assemble_to(base + ".s", ofile,
                "        .globl econst, etext, edata, ebss, end\n"
                "        .const\n"
                "cw:     .word 1, 2\n"
                "        .data\n"
                "dw:     .word 7\n"
                "        .bss\n"
                "bw:     . = . + 3\n"
                "        .text\n"
                "        .globl f\n"
                "f:      uj f\n");

    ASSERT_EQ(link_files(image, { ofile }), 0);

    auto img  = read_file(image);
    auto syms = read_symbols(img);

    // corigin = BADDR = 8, then each segment starts where the previous one ended.
    EXPECT_EQ(find_symbol(syms, "econst").value, 8L + 2);         // = torigin
    EXPECT_EQ(find_symbol(syms, "etext").value, 8L + 2 + 1);      // = dorigin
    EXPECT_EQ(find_symbol(syms, "edata").value, 8L + 2 + 1 + 1);  // = borigin
    EXPECT_EQ(find_symbol(syms, "ebss").value, 8L + 2 + 1 + 1 + 3);

    // bss is the last segment, so "past bss" and "past everything" coincide.
    EXPECT_EQ(find_symbol(syms, "end").value, find_symbol(syms, "ebss").value);

    // The old v7 spellings are gone; nothing defines them any more.  find_symbol()
    // fails the test on a miss, so check for absence directly.
    for (const char *stale : { "_econst", "_etext", "_edata", "_ebss", "_end" }) {
        auto it = std::find_if(syms.begin(), syms.end(),
                               [stale](const Sym &s) { return s.name == stale; });
        EXPECT_EQ(it, syms.end()) << "stale symbol '" << stale << "' is still defined";
    }
}
