//
// Unit tests for the half-word primitives fgeth() / fputh().
//
// These are the foundation every FILE*-based serializer is built on: each
// 24-bit field is written by fputh() and read by fgeth() in little-endian byte
// order. Round-trip tests alone cannot catch a byte-order bug here (an encode
// and a decode bug would cancel out), so the raw-bytes test pins the on-disk
// layout independently of fgeth().
//
#include <gtest/gtest.h>
#include <cstdio>

// The library is compiled as C and besm6/b.out.h has no extern "C" guard.
extern "C" {
#include "besm6/b.out.h"
}

// fputh then fgeth returns the same value, across the 24-bit range.
TEST(Half, RoundTrip) {
    const long values[] = { 0, 1, 0x123456, 0xFFFFFF };
    for (long v : values) {
        FILE *f = tmpfile();
        ASSERT_NE(f, nullptr);
        fputh(v, f);
        rewind(f);
        EXPECT_EQ(fgeth(f), v) << "value 0x" << std::hex << v;
        fclose(f);
    }
}

// fputh writes exactly 3 bytes (one half-word).
TEST(Half, EncodedSize) {
    FILE *f = tmpfile();
    ASSERT_NE(f, nullptr);
    fputh(0x123456, f);
    EXPECT_EQ(ftell(f), 3);
    fclose(f);
}

// The on-disk image is little-endian: 0x123456 -> {0x56, 0x34, 0x12}.
TEST(Half, RawBytesLittleEndian) {
    FILE *f = tmpfile();
    ASSERT_NE(f, nullptr);
    fputh(0x123456, f);
    rewind(f);

    const unsigned char expected[3] = { 0x56, 0x34, 0x12 };
    unsigned char buf[3];
    ASSERT_EQ(fread(buf, 1, 3, f), (size_t)3);
    EXPECT_EQ(getc(f), EOF);  // nothing beyond the half-word
    for (int i = 0; i < 3; i++)
        EXPECT_EQ(buf[i], expected[i]) << "byte " << i;
    fclose(f);
}

// Only the low 24 bits are written; the upper bits of the argument are dropped.
TEST(Half, Truncation) {
    FILE *f = tmpfile();
    ASSERT_NE(f, nullptr);
    fputh(0xAA123456L, f);  // 0xAA in the would-be fourth byte
    EXPECT_EQ(ftell(f), 3);  // still only 3 bytes on disk
    rewind(f);
    EXPECT_EQ(fgeth(f), 0x123456);  // upper byte does not survive
    fclose(f);
}
