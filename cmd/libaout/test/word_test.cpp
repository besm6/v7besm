//
// Unit tests for the full-word primitives fgetw() / fputw().
//
// A BESM-6 word is 48 bits == 6 bytes, stored as two little-endian 24-bit
// half-words (low half-word first). As with the half-word tests, a round-trip
// alone cannot catch a byte-order bug (an encode and a decode bug would cancel
// out), so the raw-bytes test pins the on-disk layout independently of fgetw().
//
#include <gtest/gtest.h>
#include <cstdio>

// The library is compiled as C and besm6/b.out.h has no extern "C" guard.
extern "C" {
#include "besm6/b.out.h"
}

// fputw then fgetw returns the same value, across the full 48-bit range.
TEST(Word, RoundTrip) {
    const uword_t values[] = {
        0, 1, 0x123456, 0xFFFFFF,        // up to and including the half-word boundary
        0x1000000,                       // just above the low half-word
        0x123456789ABCull, 0xFFFFFFFFFFFFull,  // full 48 bits
    };
    for (uword_t v : values) {
        FILE *f = tmpfile();
        ASSERT_NE(f, nullptr);
        fputw(v, f);
        rewind(f);
        EXPECT_EQ(fgetw(f), v) << "value 0x" << std::hex << v;
        fclose(f);
    }
}

// fputw writes exactly 6 bytes (one full word).
TEST(Word, EncodedSize) {
    FILE *f = tmpfile();
    ASSERT_NE(f, nullptr);
    fputw(0x123456789ABCull, f);
    EXPECT_EQ(ftell(f), 6);
    fclose(f);
}

// The on-disk image is little-endian: low half-word first, low byte first.
TEST(Word, RawBytesLittleEndian) {
    FILE *f = tmpfile();
    ASSERT_NE(f, nullptr);
    fputw(0x123456789ABCull, f);
    rewind(f);

    const unsigned char expected[6] = { 0xBC, 0x9A, 0x78, 0x56, 0x34, 0x12 };
    unsigned char buf[6];
    ASSERT_EQ(fread(buf, 1, 6, f), (size_t)6);
    EXPECT_EQ(getc(f), EOF);  // nothing beyond the word
    for (int i = 0; i < 6; i++)
        EXPECT_EQ(buf[i], expected[i]) << "byte " << i;
    fclose(f);
}

// Only the low 48 bits are written; the upper bits of the argument are dropped.
TEST(Word, Truncation) {
    FILE *f = tmpfile();
    ASSERT_NE(f, nullptr);
    fputw(0xAB123456789ABCull, f);  // 0xAB in the would-be seventh byte
    EXPECT_EQ(ftell(f), 6);  // still only 6 bytes on disk
    rewind(f);
    EXPECT_EQ(fgetw(f), 0x123456789ABCull);  // upper byte does not survive
    fclose(f);
}
