//
// The format floor: word conversion, character packing, and the bytes a block
// actually puts on the disk.
//
// Everything else in this tool is built on these four things being right, so the
// raw-byte cases here deliberately do NOT go through the reader.  A round trip
// only proves the encoder and decoder agree with each other; an encode/decode pair
// of matching bugs passes it and produces images no kernel can read.  The byte
// expectations are written out by hand from doc/Besm6_Data_Representation.md.
//
#include <gtest/gtest.h>

#include <cstdio>
#include <sstream>
#include <vector>

#include "image.h"

//
// A word holds a 41-bit signed value: sign in bit 41, bits 48-42 clear.
//
TEST(Word, RoundTrip)
{
    EXPECT_EQ(from_word(to_word(0)), 0);
    EXPECT_EQ(from_word(to_word(1)), 1);
    EXPECT_EQ(from_word(to_word(-1)), -1);
    EXPECT_EQ(from_word(to_word(2000)), 2000);
    EXPECT_EQ(from_word(to_word(WORD_MAX)), WORD_MAX);
    EXPECT_EQ(from_word(to_word(WORD_MIN)), WORD_MIN);
}

//
// The boundary at bit 41, where sign extension starts.
//
TEST(Word, SignBoundary)
{
    EXPECT_EQ(from_word(to_word(WORD_MAX)), (int64_t(1) << 40) - 1);
    EXPECT_EQ(to_word(-1), VALUE_MSK);
    EXPECT_EQ(from_word(SIGN_BIT), WORD_MIN);
    EXPECT_EQ(from_word(SIGN_BIT - 1), WORD_MAX);
}

//
// Out of range REPORTS.  It must not wrap: the commonest source of an oversized
// value is a host file bigger than the target's off_t can name, and truncating
// that produces an image whose inode says one thing and whose blocks say another.
//
TEST(Word, OverflowIsRejected)
{
    EXPECT_THROW(to_word(WORD_MAX + 1), FsError);
    EXPECT_THROW(to_word(WORD_MIN - 1), FsError);
    EXPECT_THROW(to_word(int64_t(1) << 62), FsError);
    EXPECT_NO_THROW(to_word(WORD_MAX));
    EXPECT_NO_THROW(to_word(WORD_MIN));
}

//
// Six chars to a word, byte 0 in bits 48-41 -- shift distance 40 - 8k.
//
TEST(Word, PackChars)
{
    EXPECT_EQ(pack_chars("abcdef", 6), 0x616263646566ULL);
    EXPECT_EQ(pack_chars(".", 6), 0x2E0000000000ULL);
    EXPECT_EQ(pack_chars("..", 6), 0x2E2E00000000ULL);
    EXPECT_EQ(pack_chars("", 6), 0ULL);

    // A word is filled left to right: 'a' is the MOST significant byte.
    EXPECT_EQ(pack_chars("a", 6) >> 40, 'a');
}

TEST(Word, UnpackChars)
{
    char buf[NBPW + 1] = {};
    unpack_chars(0x616263646566ULL, buf, NBPW);
    EXPECT_STREQ(buf, "abcdef");

    char dot[NBPW + 1] = {};
    unpack_chars(pack_chars(".", 6), dot, NBPW);
    EXPECT_STREQ(dot, ".");
}

//
// The whole 18-char name, across its three words.
//
TEST(Word, PackFullName)
{
    const char *name = "abcdefghijklmnopqr";
    EXPECT_EQ(pack_chars(name + 0, 6), 0x616263646566ULL);
    EXPECT_EQ(pack_chars(name + 6, 6), 0x6768696A6B6CULL);
    EXPECT_EQ(pack_chars(name + 12, 6), 0x6D6E6F707172ULL);
}

namespace {

//
// Read a file back as raw bytes, without going anywhere near Image's reader.
//
std::vector<uint8_t> slurp(const char *path)
{
    std::FILE *f = std::fopen(path, "rb");
    EXPECT_NE(f, nullptr) << path;
    std::vector<uint8_t> bytes;
    int c;
    while ((c = std::fgetc(f)) != EOF)
        bytes.push_back(uint8_t(c));
    std::fclose(f);
    return bytes;
}

} // namespace

//
// THE byte-order test.  A word is six bytes, big-endian, most significant first.
// Compared against a hand-written expectation, not against Image's own reader.
//
TEST(Image, RawBytesAreBigEndian)
{
    const char *path = "word_raw.img";

    Block b{};
    b[0] = 0x123456789ABCULL;
    b[1] = 0x000000000001ULL;
    b[2] = WORD_MASK;

    {
        Image img;
        img.create(path, 3);
        img.write_block(0, b);
    }

    const std::vector<uint8_t> raw = slurp(path);
    ASSERT_EQ(raw.size(), size_t(3 * BSIZE));

    // Word 0: the six bytes read as one big-endian 48-bit number.
    EXPECT_EQ(raw[0], 0x12);
    EXPECT_EQ(raw[1], 0x34);
    EXPECT_EQ(raw[2], 0x56);
    EXPECT_EQ(raw[3], 0x78);
    EXPECT_EQ(raw[4], 0x9A);
    EXPECT_EQ(raw[5], 0xBC);

    // Word 1: the value 1 lives in the LAST byte.
    EXPECT_EQ(raw[6], 0x00);
    EXPECT_EQ(raw[11], 0x01);

    // Word 2: all 48 bits set, and no more -- the host's top 16 bits never escape.
    for (int i = 12; i < 18; i++)
        EXPECT_EQ(raw[i], 0xFF) << "byte " << i;

    std::remove(path);
}

//
// A block is BSIZEW words and occupies exactly BSIZE bytes; block n starts at
// byte n * BSIZE and nothing is interleaved between them.
//
TEST(Image, BlockPlacementIsFlat)
{
    const char *path = "word_flat.img";

    Block one{}, two{};
    one[0] = 0x111111111111ULL;
    two[0] = 0x222222222222ULL;

    {
        Image img;
        img.create(path, 4);
        img.write_block(1, one);
        img.write_block(3, two);
    }

    const std::vector<uint8_t> raw = slurp(path);
    ASSERT_EQ(raw.size(), size_t(4 * BSIZE));
    EXPECT_EQ(raw[1 * BSIZE], 0x11);
    EXPECT_EQ(raw[3 * BSIZE], 0x22);
    EXPECT_EQ(raw[0], 0x00); // block 0 untouched
    EXPECT_EQ(raw[2 * BSIZE], 0x00);

    std::remove(path);
}

TEST(Image, ReadWriteRoundTrip)
{
    const char *path = "word_rt.img";

    Block out{};
    for (int i = 0; i < BSIZEW; i++)
        out[i] = Word(i) * 0x10000001ULL & WORD_MASK;

    Image img;
    img.create(path, 10);
    img.write_block(7, out);

    Block in{};
    img.read_block(7, in);
    EXPECT_EQ(in, out);

    img.close();
    std::remove(path);
}

//
// A file whose size is not a whole number of blocks is not an image.  Saying so at
// open time turns a mystifying short read deep in the free-list walk into one line.
//
TEST(Image, RejectsPartialBlock)
{
    const char *path = "word_short.img";

    std::FILE *f = std::fopen(path, "wb");
    ASSERT_NE(f, nullptr);
    for (int i = 0; i < BSIZE + 7; i++)
        std::fputc(0, f);
    std::fclose(f);

    Image img;
    EXPECT_THROW(img.open(path, false), FsError);

    std::remove(path);
}

TEST(Image, RejectsOutOfRangeBlock)
{
    const char *path = "word_range.img";

    Image img;
    img.create(path, 5);

    Block b{};
    EXPECT_THROW(img.write_block(5, b), FsError);
    EXPECT_THROW(img.write_block(-1, b), FsError);
    EXPECT_THROW(img.read_block(99, b), FsError);
    EXPECT_NO_THROW(img.write_block(4, b));

    img.close();
    std::remove(path);
}

//
// A read-only image refuses writes rather than failing silently at fclose().
//
TEST(Image, ReadOnlyRefusesWrites)
{
    const char *path = "word_ro.img";

    {
        Image img;
        img.create(path, 3);
    }

    Image img;
    img.open(path, false);
    EXPECT_FALSE(img.writable());
    EXPECT_EQ(img.nblocks(), 3);

    Block b{};
    EXPECT_THROW(img.write_block(0, b), FsError);

    img.close();
    std::remove(path);
}
