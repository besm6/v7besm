//
// Unit tests for the symbol table serializers fputsym() / fgetsym().
//
// A symbol is stored as: a 1-byte name length, a 1-byte type, a 3-byte
// little-endian value, then n_len name bytes (no trailing NUL). fgetsym()
// allocates and NUL-terminates n_name, and returns the on-disk entry size
// (n_len + 5) -- or 1 for an empty (zero-length) terminator entry.
//
#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// The library is compiled as C and besm6/b.out.h has no extern "C" guard.
extern "C" {
#include "besm6/b.out.h"
}

// Build a representative symbol. n_value is kept within 24 bits, the width of
// the half-word fputh()/fgeth() serialize.
static struct nlist sample_sym(char *name)
{
    struct nlist s{};
    s.n_len   = static_cast<short>(std::strlen(name));
    s.n_type  = N_TEXT | N_EXT;
    s.n_value = 0x123456;
    s.n_name  = name;
    return s;
}

// Write a symbol, read it back: every field matches and the name round-trips.
TEST(Sym, RoundTrip) {
    char name[] = "_main";
    struct nlist out = sample_sym(name);

    FILE *f = tmpfile();
    ASSERT_NE(f, nullptr);
    fputsym(&out, f);
    rewind(f);

    struct nlist in{};
    EXPECT_EQ(fgetsym(f, &in), out.n_len + 5);
    EXPECT_EQ(in.n_len,   out.n_len);
    EXPECT_EQ(in.n_type,  out.n_type);
    EXPECT_EQ(in.n_value, out.n_value);
    ASSERT_NE(in.n_name, nullptr);
    EXPECT_STREQ(in.n_name, name);
    std::free(in.n_name);
    fclose(f);
}

// fgetsym NUL-terminates the heap-allocated name at offset n_len.
TEST(Sym, NulTerminated) {
    char name[] = "abc";
    struct nlist out = sample_sym(name);

    FILE *f = tmpfile();
    ASSERT_NE(f, nullptr);
    fputsym(&out, f);
    rewind(f);

    struct nlist in{};
    ASSERT_EQ(fgetsym(f, &in), out.n_len + 5);
    ASSERT_NE(in.n_name, nullptr);
    EXPECT_EQ(in.n_name[in.n_len], '\0');
    std::free(in.n_name);
    fclose(f);
}

// The on-disk layout is: len, type, 3-byte LE value, then the name bytes with
// no trailing NUL.
TEST(Sym, RawBytes) {
    char name[] = "ab";
    struct nlist out = sample_sym(name);  // n_len 2, type 043, value 0x123456

    FILE *f = tmpfile();
    ASSERT_NE(f, nullptr);
    fputsym(&out, f);
    rewind(f);

    const unsigned char expected[] = {
        0x02,                    // n_len
        N_TEXT | N_EXT,          // n_type (043)
        0x56, 0x34, 0x12,        // n_value = 0x123456, little-endian
        'a', 'b',                // name bytes, no trailing NUL
    };
    unsigned char buf[sizeof(expected)];
    ASSERT_EQ(fread(buf, 1, sizeof(buf), f), sizeof(buf));
    EXPECT_EQ(getc(f), EOF);  // nothing beyond the name
    for (size_t i = 0; i < sizeof(expected); i++)
        EXPECT_EQ(buf[i], expected[i]) << "byte " << i;
    fclose(f);
}

// A zero-length entry is the table terminator: fgetsym returns 1 and allocates
// nothing.
TEST(Sym, EmptyEntryAtEof) {
    FILE *f = tmpfile();
    ASSERT_NE(f, nullptr);
    putc(0, f);  // single zero length byte
    rewind(f);

    struct nlist in{};
    in.n_name = nullptr;
    EXPECT_EQ(fgetsym(f, &in), 1);
    EXPECT_EQ(in.n_name, nullptr);  // no allocation for an empty entry
    fclose(f);
}

// Two symbols written back-to-back read back in order, with the stream
// advancing past the first to the second.
TEST(Sym, MultipleSequential) {
    char n1[] = "first";
    char n2[] = "second";
    struct nlist a = sample_sym(n1);
    struct nlist b = sample_sym(n2);
    b.n_value = 0xABCDE;

    FILE *f = tmpfile();
    ASSERT_NE(f, nullptr);
    fputsym(&a, f);
    fputsym(&b, f);
    rewind(f);

    struct nlist first{};
    ASSERT_EQ(fgetsym(f, &first), a.n_len + 5);
    EXPECT_STREQ(first.n_name, n1);
    EXPECT_EQ(first.n_value, a.n_value);
    std::free(first.n_name);

    struct nlist second{};
    ASSERT_EQ(fgetsym(f, &second), b.n_len + 5);
    EXPECT_STREQ(second.n_name, n2);
    EXPECT_EQ(second.n_value, b.n_value);
    std::free(second.n_name);
    fclose(f);
}
