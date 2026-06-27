//
// Unit tests for the a.out header serializers fputhdr() / fgethdr().
//
#include <gtest/gtest.h>
#include <cstdio>

// The library is compiled as C and besm6/b.out.h has no extern "C" guard,
// so wrap the include to keep C linkage for fputhdr/fgethdr/fgeth.
extern "C" {
#include "besm6/b.out.h"
}

// Build a representative header. All field values are kept within 24 bits,
// because fputh()/fgeth() serialize each field as a 24-bit little-endian
// half-word (see cmd/libaout/fputh.c), so only the low 24 bits round-trip.
static struct exec sample_header()
{
    struct exec h{};
    h.a_magic = FMAGIC;
    h.a_const = 6;
    h.a_text  = 0x123456;
    h.a_data  = 12;
    h.a_bss   = 18;
    h.a_abss  = 24;
    h.a_syms  = 30;
    h.a_entry = 0xABCDE;
    h.a_flag  = RELFLG;
    return h;
}

static void expect_eq_header(const struct exec &a, const struct exec &b)
{
    EXPECT_EQ(a.a_magic, b.a_magic);
    EXPECT_EQ(a.a_const, b.a_const);
    EXPECT_EQ(a.a_text,  b.a_text);
    EXPECT_EQ(a.a_data,  b.a_data);
    EXPECT_EQ(a.a_bss,   b.a_bss);
    EXPECT_EQ(a.a_abss,  b.a_abss);
    EXPECT_EQ(a.a_syms,  b.a_syms);
    EXPECT_EQ(a.a_entry, b.a_entry);
    EXPECT_EQ(a.a_flag,  b.a_flag);
}

// Write a header, read it back, every field matches.
TEST(Header, RoundTrip) {
    struct exec out = sample_header();
    FILE *f = tmpfile();
    ASSERT_NE(f, nullptr);

    fputhdr(&out, f);
    rewind(f);

    struct exec in{};
    EXPECT_EQ(fgethdr(f, &in), 1);
    expect_eq_header(out, in);
    fclose(f);
}

// fputhdr writes exactly HDRSZ (54) bytes.
TEST(Header, EncodedSize) {
    struct exec out = sample_header();
    FILE *f = tmpfile();
    ASSERT_NE(f, nullptr);

    fputhdr(&out, f);
    EXPECT_EQ(ftell(f), HDRSZ);
    fclose(f);
}

// A zeroed header round-trips too.
TEST(Header, ZeroRoundTrip) {
    struct exec out{};
    FILE *f = tmpfile();
    ASSERT_NE(f, nullptr);

    fputhdr(&out, f);
    rewind(f);

    struct exec in{};
    EXPECT_EQ(fgethdr(f, &in), 1);
    expect_eq_header(out, in);
    fclose(f);
}

// The on-disk image is exactly the bytes we expect: for each of the 9 fields,
// a 3-byte little-endian value followed by 3 zero pad bytes, in header order
// (magic, const, text, data, bss, abss, syms, entry, flag).
TEST(Header, RawBytes) {
    struct exec out = sample_header();
    FILE *f = tmpfile();
    ASSERT_NE(f, nullptr);

    fputhdr(&out, f);
    rewind(f);

    // FMAGIC == 0407, a_text == 0x123456, a_entry == 0xABCDE, a_flag == 1.
    const unsigned char expected[HDRSZ] = {
        0x07, 0x01, 0x00,  0x00, 0x00, 0x00,   // a_magic = 0407
        0x06, 0x00, 0x00,  0x00, 0x00, 0x00,   // a_const = 6
        0x56, 0x34, 0x12,  0x00, 0x00, 0x00,   // a_text  = 0x123456
        0x0C, 0x00, 0x00,  0x00, 0x00, 0x00,   // a_data  = 12
        0x12, 0x00, 0x00,  0x00, 0x00, 0x00,   // a_bss   = 18
        0x18, 0x00, 0x00,  0x00, 0x00, 0x00,   // a_abss  = 24
        0x1E, 0x00, 0x00,  0x00, 0x00, 0x00,   // a_syms  = 30
        0xDE, 0xBC, 0x0A,  0x00, 0x00, 0x00,   // a_entry = 0xABCDE
        0x01, 0x00, 0x00,  0x00, 0x00, 0x00,   // a_flag  = RELFLG
    };

    unsigned char buf[HDRSZ];
    ASSERT_EQ(fread(buf, 1, HDRSZ, f), (size_t)HDRSZ);
    EXPECT_EQ(getc(f), EOF);  // nothing beyond the header
    for (int i = 0; i < HDRSZ; i++)
        EXPECT_EQ(buf[i], expected[i]) << "byte " << i;
    fclose(f);
}

// Each 6-byte field group is a 3-byte value followed by 3 zero pad bytes.
TEST(Header, PadBytesAreZero) {
    struct exec out = sample_header();
    FILE *f = tmpfile();
    ASSERT_NE(f, nullptr);

    fputhdr(&out, f);
    rewind(f);

    unsigned char buf[HDRSZ];
    ASSERT_EQ(fread(buf, 1, HDRSZ, f), (size_t)HDRSZ);
    for (int field = 0; field < HDRSZ / 6; field++) {
        EXPECT_EQ(buf[field * 6 + 3], 0) << "field " << field;
        EXPECT_EQ(buf[field * 6 + 4], 0) << "field " << field;
        EXPECT_EQ(buf[field * 6 + 5], 0) << "field " << field;
    }
    fclose(f);
}
