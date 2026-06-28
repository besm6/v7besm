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

// Assemble a multi-line source and return the resulting object file bytes.
static std::vector<unsigned char> assemble(const std::string &source)
{
    std::string base    = current_test_name();
    std::string infile  = base + ".s";
    std::string outfile = base + ".o";

    {
        std::ofstream src(infile, std::ios::trunc);
        src << source;
    }

    struct assembler_args args = {};
    args.infile  = const_cast<char *>(infile.c_str());
    args.outfile = const_cast<char *>(outfile.c_str());
    EXPECT_EQ(assemble(&args), 0);

    std::ifstream obj(outfile, std::ios::binary);
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(obj)),
                                      std::istreambuf_iterator<char>());
}

// High 24-bit half-word stored at word index `w` of the file (a BESM-6 word is
// two big-endian half-words; the high/left half-word executes first).
static long word_high(const std::vector<unsigned char> &b, int w)
{
    size_t o = (size_t)w * 6;
    return ((long)b[o] << 16) | ((long)b[o + 1] << 8) | b[o + 2];
}

// Low 24-bit half-word stored at word index `w` of the file (a BESM-6 word is
// two big-endian half-words; header fields live in the low half-word).
static long word_low(const std::vector<unsigned char> &b, int w)
{
    size_t o = (size_t)w * 6 + 3;
    return ((long)b[o] << 16) | ((long)b[o + 1] << 8) | b[o + 2];
}

// Assemble every machine instruction from the assembler's table[] and check the
// opcode emitted for each one.  Two 24-bit instructions pack into one 48-bit
// word: the first lands in the high (left, executed-first) half-word, the second
// in the low half-word.  The three TALIGN instructions (vjm/ij/stop) cause a utc
// (02200000) filler to be inserted in the low half so the following instruction
// stays word-aligned.  Text begins at file word 9 (a_entry/HDRSZ).
TEST(Assemble, AllInstructions)
{
    std::vector<unsigned char> got = assemble(R"(
        atx
        stx
        mod
        xts
        a+x
        a-x
        x-a
        amx
        xta
        aax
        aex
        arx
        avx
        aox
        a/x
        a*x
        apx
        aux
        acx
        anx
        e+x
        e-x
        asx
        xtr
        rte
        yta
        e32
        e33
        e+n
        e-n
        asn
        ntr
        ati
        sti
        ita
        its
        mtj
        j+m
        e46
        e47
        e50
        e51
        e52
        e53
        e54
        e55
        e56
        e57
        e60
        e61
        e62
        e63
        e64
        e65
        e66
        e67
        e70
        e71
        e72
        e73
        e74
        e75
        e76
        e77
        e20
        e21
        utc
        wtc
        vtm
        utm
        uza
        u1a
        uj
        vjm
        ij
        stop
        vzm
        v1m
        e36
        vlm
)");

    // Header sanity, so a gross regression points at the offending field.
    EXPECT_EQ(word_low(got, 0), 0407); // a_magic == FMAGIC
    EXPECT_EQ(word_low(got, 2), 246);  // a_text  == 41 words * 6 bytes

    // Short-address instructions (opcodes 000-077, val = opcode << 12).
    EXPECT_EQ(word_high(got, 9), 00000000L);  // atx
    EXPECT_EQ(word_low(got, 9), 00010000L);   // stx
    EXPECT_EQ(word_high(got, 10), 00020000L); // mod
    EXPECT_EQ(word_low(got, 10), 00030000L);  // xts
    EXPECT_EQ(word_high(got, 11), 00040000L); // a+x
    EXPECT_EQ(word_low(got, 11), 00050000L);  // a-x
    EXPECT_EQ(word_high(got, 12), 00060000L); // x-a
    EXPECT_EQ(word_low(got, 12), 00070000L);  // amx
    EXPECT_EQ(word_high(got, 13), 00100000L); // xta
    EXPECT_EQ(word_low(got, 13), 00110000L);  // aax
    EXPECT_EQ(word_high(got, 14), 00120000L); // aex
    EXPECT_EQ(word_low(got, 14), 00130000L);  // arx
    EXPECT_EQ(word_high(got, 15), 00140000L); // avx
    EXPECT_EQ(word_low(got, 15), 00150000L);  // aox
    EXPECT_EQ(word_high(got, 16), 00160000L); // a/x
    EXPECT_EQ(word_low(got, 16), 00170000L);  // a*x
    EXPECT_EQ(word_high(got, 17), 00200000L); // apx
    EXPECT_EQ(word_low(got, 17), 00210000L);  // aux
    EXPECT_EQ(word_high(got, 18), 00220000L); // acx
    EXPECT_EQ(word_low(got, 18), 00230000L);  // anx
    EXPECT_EQ(word_high(got, 19), 00240000L); // e+x
    EXPECT_EQ(word_low(got, 19), 00250000L);  // e-x
    EXPECT_EQ(word_high(got, 20), 00260000L); // asx
    EXPECT_EQ(word_low(got, 20), 00270000L);  // xtr
    EXPECT_EQ(word_high(got, 21), 00300000L); // rte
    EXPECT_EQ(word_low(got, 21), 00310000L);  // yta
    EXPECT_EQ(word_high(got, 22), 00320000L); // e32
    EXPECT_EQ(word_low(got, 22), 00330000L);  // e33
    EXPECT_EQ(word_high(got, 23), 00340000L); // e+n
    EXPECT_EQ(word_low(got, 23), 00350000L);  // e-n
    EXPECT_EQ(word_high(got, 24), 00360000L); // asn
    EXPECT_EQ(word_low(got, 24), 00370000L);  // ntr
    EXPECT_EQ(word_high(got, 25), 00400000L); // ati
    EXPECT_EQ(word_low(got, 25), 00410000L);  // sti
    EXPECT_EQ(word_high(got, 26), 00420000L); // ita
    EXPECT_EQ(word_low(got, 26), 00430000L);  // its
    EXPECT_EQ(word_high(got, 27), 00440000L); // mtj
    EXPECT_EQ(word_low(got, 27), 00450000L);  // j+m
    EXPECT_EQ(word_high(got, 28), 00460000L); // e46
    EXPECT_EQ(word_low(got, 28), 00470000L);  // e47
    EXPECT_EQ(word_high(got, 29), 00500000L); // e50
    EXPECT_EQ(word_low(got, 29), 00510000L);  // e51
    EXPECT_EQ(word_high(got, 30), 00520000L); // e52
    EXPECT_EQ(word_low(got, 30), 00530000L);  // e53
    EXPECT_EQ(word_high(got, 31), 00540000L); // e54
    EXPECT_EQ(word_low(got, 31), 00550000L);  // e55
    EXPECT_EQ(word_high(got, 32), 00560000L); // e56
    EXPECT_EQ(word_low(got, 32), 00570000L);  // e57
    EXPECT_EQ(word_high(got, 33), 00600000L); // e60
    EXPECT_EQ(word_low(got, 33), 00610000L);  // e61
    EXPECT_EQ(word_high(got, 34), 00620000L); // e62
    EXPECT_EQ(word_low(got, 34), 00630000L);  // e63
    EXPECT_EQ(word_high(got, 35), 00640000L); // e64
    EXPECT_EQ(word_low(got, 35), 00650000L);  // e65
    EXPECT_EQ(word_high(got, 36), 00660000L); // e66
    EXPECT_EQ(word_low(got, 36), 00670000L);  // e67
    EXPECT_EQ(word_high(got, 37), 00700000L); // e70
    EXPECT_EQ(word_low(got, 37), 00710000L);  // e71
    EXPECT_EQ(word_high(got, 38), 00720000L); // e72
    EXPECT_EQ(word_low(got, 38), 00730000L);  // e73
    EXPECT_EQ(word_high(got, 39), 00740000L); // e74
    EXPECT_EQ(word_low(got, 39), 00750000L);  // e75
    EXPECT_EQ(word_high(got, 40), 00760000L); // e76
    EXPECT_EQ(word_low(got, 40), 00770000L);  // e77

    // Long-address instructions (opcodes 020-037, val = opcode << 15).
    EXPECT_EQ(word_high(got, 41), 02000000L); // e20
    EXPECT_EQ(word_low(got, 41), 02100000L);  // e21
    EXPECT_EQ(word_high(got, 42), 02200000L); // utc
    EXPECT_EQ(word_low(got, 42), 02300000L);  // wtc
    EXPECT_EQ(word_high(got, 43), 02400000L); // vtm
    EXPECT_EQ(word_low(got, 43), 02500000L);  // utm
    EXPECT_EQ(word_high(got, 44), 02600000L); // uza
    EXPECT_EQ(word_low(got, 44), 02700000L);  // u1a
    EXPECT_EQ(word_high(got, 45), 03000000L); // uj
    EXPECT_EQ(word_low(got, 45), 03100000L);  // vjm
    EXPECT_EQ(word_high(got, 46), 03200000L); // ij
    EXPECT_EQ(word_low(got, 46), 02200000L);  // utc filler (align after ij)
    EXPECT_EQ(word_high(got, 47), 03300000L); // stop
    EXPECT_EQ(word_low(got, 47), 02200000L);  // utc filler (align after stop)
    EXPECT_EQ(word_high(got, 48), 03400000L); // vzm
    EXPECT_EQ(word_low(got, 48), 03500000L);  // v1m
    EXPECT_EQ(word_high(got, 49), 03600000L); // e36
    EXPECT_EQ(word_low(got, 49), 03700000L);  // vlm
}
