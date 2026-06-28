//
// Unit tests for the cmd/as assembler engine.
//
// The engine is driven through assemble(&args), which reads an input file and
// writes a BESM-6 a.out object.  Each test names its input/output files after
// the running test (guaranteed unique) and leaves them on disk as debugging
// artefacts.
//
#include <gtest/gtest.h>

#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

// The engine library is compiled as C; assemble.h is C++-safe.
extern "C" {
#include "assemble.h"
}

// Replace the front-end uerror(): instead of exit(1), surface the assembler
// error as a GoogleTest failure via an uncaught exception.
extern "C" [[noreturn]] void uerror(char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    throw std::runtime_error(buf);
}

// Name of the currently running test, e.g. "Assemble.StopInstruction".
static std::string current_test_name()
{
    const auto *info = ::testing::UnitTest::GetInstance()->current_test_info();
    return std::string(info->test_suite_name()) + "." + info->name();
}

// Assemble a single source line and return the resulting object file bytes.
static std::vector<unsigned char> assemble_line(const std::string &line)
{
    std::string base    = current_test_name();
    std::string infile  = base + ".s";
    std::string outfile = base + ".o";

    {
        std::ofstream src(infile, std::ios::trunc);
        src << line << "\n";
    }

    struct assembler_args args = {};
    args.infile  = const_cast<char *>(infile.c_str());
    args.outfile = const_cast<char *>(outfile.c_str());
    EXPECT_EQ(assemble(&args), 0);

    std::ifstream obj(outfile, std::ios::binary);
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(obj)),
                                      std::istreambuf_iterator<char>());
}

// Low 24-bit half-word stored at word index `w` of the file (a BESM-6 word is
// two big-endian half-words; header fields live in the low half-word).
static long word_low(const std::vector<unsigned char> &b, int w)
{
    size_t o = (size_t)w * 6 + 3;
    return ((long)b[o] << 16) | ((long)b[o + 1] << 8) | b[o + 2];
}

// Assembling a lone "stop" instruction produces the expected a.out bytes.
TEST(Assemble, StopInstruction)
{
    // a.out for a single aligned "stop" (octal 03300000, opcode in the low
    // half-word, an utc/02200000 filler in the high half-word of the text word):
    //   header  (9 words, 54 bytes): magic=0407, a_text=6, a_syms=6, a_entry=9
    //   text    (1 word): 02200000 02200000-filler << 24 | 03300000 stop
    //   symtab  (1 word): alignment padding
    //   strtab  (1 word)
    const std::vector<unsigned char> expected = {
        0x00, 0x00, 0x00, 0x00, 0x01, 0x07, // a_magic = 0407 (FMAGIC)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // a_const = 0
        0x00, 0x00, 0x00, 0x00, 0x00, 0x06, // a_text  = 6
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // a_data  = 0
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // a_bss   = 0
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // a_abss  = 0
        0x00, 0x00, 0x00, 0x00, 0x00, 0x06, // a_syms  = 6
        0x00, 0x00, 0x00, 0x00, 0x00, 0x09, // a_entry = 9 (HDRSZ/W)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // a_flag  = 0
        0x09, 0x00, 0x00, 0x0d, 0x80, 0x00, // text: filler 02200000 | stop 03300000
        0x00, 0x00, 0x00, 0x00, 0x00, 0x06, // symbol table (padding)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // string table
    };

    std::vector<unsigned char> got = assemble_line("stop");

    // Targeted checks first, so a regression points at the offending field
    // rather than just "the bytes differ".
    ASSERT_EQ(got.size(), expected.size());
    EXPECT_EQ(word_low(got, 0), 0407); // a_magic == FMAGIC
    EXPECT_EQ(word_low(got, 2), 6);    // a_text  == one word
    EXPECT_EQ(word_low(got, 9), 03300000); // "stop" opcode in the text word

    EXPECT_EQ(got, expected);
}
