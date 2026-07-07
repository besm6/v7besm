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

#include <cerrno>
#include <cstdlib>
#include <fstream>
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
