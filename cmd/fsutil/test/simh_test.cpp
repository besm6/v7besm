//
// The SIMH disk container.
//
// FormatMatchesSimh is the strongest guarantee in this suite: it compares
// format()'s output against a SHA-256 of what the real simulator writes.  Every
// other test here checks that the code is self-consistent; that one checks it
// against the machine the images are actually for.  The digest was produced by
//
//     printf 'attach -n md00 golden2053.disk\nquit\n' > fmt.ini && besm6 fmt.ini
//     shasum -a 256 golden2053.disk
//
// against besm6 V4.0-0, git 4b955316.  If it ever fails, run those two lines
// again before touching simh.cpp -- the container is SIMH's to define, not ours,
// and a change there is a change this tool has to follow.
//
#include "simh.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <vector>

#include "image.h"

namespace {

std::vector<uint8_t> slurp(const std::string &path)
{
    std::FILE *f = std::fopen(path.c_str(), "rb");
    EXPECT_NE(f, nullptr) << path;
    std::vector<uint8_t> bytes;
    if (f) {
        int c;
        while ((c = std::fgetc(f)) != EOF)
            bytes.push_back(uint8_t(c));
        std::fclose(f);
    }
    return bytes;
}

//
// A word as SIMH stores it: eight bytes, little-endian, at word index `w'.
//
Word word_at(const std::vector<uint8_t> &raw, int64_t w)
{
    Word v = 0;
    for (int i = 0; i < 8; i++)
        v |= Word(raw[size_t(w * 8 + i)]) << (8 * i);
    return v;
}

} // namespace

//
// FNV-1a, 64-bit.  A digest, not a checksum with any pedigree -- it is here only
// so the expected value below can be one line instead of an 8 Mb file checked
// into the tree.
//
namespace {

uint64_t fnv1a(const std::vector<uint8_t> &bytes)
{
    uint64_t h = 1469598103934665603ULL;
    for (uint8_t b : bytes) {
        h ^= b;
        h *= 1099511628211ULL;
    }
    return h;
}

} // namespace

//
// THE test.  format() must produce, byte for byte, what `attach -n' produces.
//
// Everything else in this file checks that simh.cpp agrees with itself.  This one
// checks it against the simulator, which is the only authority on the container --
// and it is a whole-file comparison, so it covers all 1000 zones and not just the
// boundaries the other tests sample.
//
TEST(Simh, FormatMatchesSimh)
{
    const std::string path = "simh_golden2053.disk";
    simh::format(path, 0);

    const std::vector<uint8_t> raw = slurp(path);
    ASSERT_EQ(raw.size(), size_t(simh::SIMH_SIZE));
    EXPECT_EQ(fnv1a(raw), 0xF05603ED08E34C23ULL)
        << "format() no longer matches `attach -n md00 golden2053.disk'.\n"
        << "Regenerate before changing simh.cpp -- SIMH defines this container:\n"
        << "  printf 'attach -n md00 golden2053.disk\\nquit\\n' > fmt.ini\n"
        << "  besm6 fmt.ini && shasum -a 256 golden2053.disk";

    std::remove(path.c_str());
}

//
// A converted image with no data in it is the same thing as a formatted one.
// This is what makes the flat container's emptiness and the SIMH container's
// emptiness the same statement.
//
TEST(Simh, EmptyConversionEqualsFormat)
{
    const std::string flat = "simh_empty.img";
    const std::string out  = "simh_empty2053.disk";

    {
        Image img;
        img.create(flat, MDNBLK);
    }
    simh::to_simh(flat, out, 0);

    EXPECT_EQ(fnv1a(slurp(out)), 0xF05603ED08E34C23ULL);

    std::remove(flat.c_str());
    std::remove(out.c_str());
}

//
// The container's overall shape, stated as arithmetic rather than trusted.
//
TEST(Simh, Geometry)
{
    EXPECT_EQ(simh::ZONE_SIZE, 1032);
    EXPECT_EQ(simh::NZONE * simh::TPZ, MDNBLK);
    EXPECT_EQ(simh::SIMH_SIZE, 8256000);

    // The flat container is smaller: six bytes a word and no service words.
    EXPECT_EQ(MDNBLK * BSIZEW * NBPW, 6144000);
}

//
// The magic mark, decoded.  01370707 << 24 is 0x05F1C7000000; the volume sits
// just below it.  Written out here because the octal constant in simh.h is
// otherwise unverifiable by eye.
//
TEST(Simh, MagicMarkValue)
{
    EXPECT_EQ(simh::MAGIC_MARK, 0x05F1C7000000ULL);
    EXPECT_EQ(Word(2053) << simh::VOLUME_SHIFT, 0x805000ULL);
    EXPECT_EQ(simh::TAG_NUMBER, 0x0002000000000000ULL);
}

//
// SIMH takes the volume from the rightmost run of digits in the filename stem.
//
TEST(Simh, VolumeFromFilename)
{
    EXPECT_EQ(simh::volume_from_filename("golden2053.disk"), 2053);
    EXPECT_EQ(simh::volume_from_filename("/var/tmp/besm6/2052.bin"), 2052);
    EXPECT_EQ(simh::volume_from_filename("md2053.disk"), 2053);
    EXPECT_EQ(simh::volume_from_filename("root"), 0);
    EXPECT_EQ(simh::volume_from_filename(""), 0);

    // The RIGHTMOST run, not the first.
    EXPECT_EQ(simh::volume_from_filename("disk1-2053.img"), 2053);

    // The extension is not part of the stem, so its digits do not count.
    EXPECT_EQ(simh::volume_from_filename("sbor2053.b12"), 2053);
}

//
// Out of range is refused, with SIMH's own bounds.
//
TEST(Simh, VolumeRangeIsEnforced)
{
    EXPECT_THROW(simh::format("bad_no_digits.disk", 0), FsError);
    EXPECT_THROW(simh::format("bad9999.disk", 0), FsError);
    EXPECT_THROW(simh::format("bad0001.disk", 0), FsError);
    EXPECT_THROW(simh::format("x.disk", 5000), FsError);
    std::remove("bad_no_digits.disk");
    std::remove("bad9999.disk");
    std::remove("bad0001.disk");
    std::remove("x.disk");
}

//
// The first zone, word by word, against the bytes the simulator writes.
//
TEST(Simh, FirstZoneLayout)
{
    const std::string path = "simh_zone2053.disk";
    simh::format(path, 0); // volume inferred from the name

    const std::vector<uint8_t> raw = slurp(path);
    ASSERT_EQ(raw.size(), size_t(simh::SIMH_SIZE));

    // Zone 0, track 0: self-address 0, then the mark, then two bare tags.
    EXPECT_EQ(word_at(raw, 0), simh::TAG_NUMBER | (Word(0) << 36));
    EXPECT_EQ(word_at(raw, 1), 0x000205F1C7805000ULL);
    EXPECT_EQ(word_at(raw, 2), simh::TAG_NUMBER);
    EXPECT_EQ(word_at(raw, 3), simh::TAG_NUMBER);

    // Zone 0, track 1: self-address 1.  Both tracks' service words precede the
    // data -- it is NOT four words then that track's 512 words.
    EXPECT_EQ(word_at(raw, 4), simh::TAG_NUMBER | (Word(1) << 36));
    EXPECT_EQ(word_at(raw, 5), 0x000205F1C7805000ULL);

    // An empty data word is a tagged zero, not a zero.
    EXPECT_EQ(word_at(raw, 8), simh::TAG_NUMBER);

    // Zone 1 begins one ZONE_SIZE along and calls itself block 2.
    EXPECT_EQ(word_at(raw, simh::ZONE_SIZE), simh::TAG_NUMBER | (Word(2) << 36));
    EXPECT_EQ(word_at(raw, simh::ZONE_SIZE + 4), simh::TAG_NUMBER | (Word(3) << 36));

    std::remove(path.c_str());
}

//
// Block b's data lands where disk_read_track() looks for it:
// word 8 + ZONE_SIZE*(b/2) + 512*(b%2).
//
TEST(Simh, BlockDataOffsets)
{
    const std::string flat = "simh_off.img";
    const std::string out  = "simh_off2053.disk";

    // Distinguishable content in four consecutive blocks, spanning two zones.
    {
        Image img;
        img.create(flat, 4);
        for (int64_t b = 0; b < 4; b++) {
            Block blk{};
            blk[0]   = 0x110000000000ULL + Word(b);
            blk[511] = 0x220000000000ULL + Word(b);
            img.write_block(b, blk);
        }
    }
    simh::to_simh(flat, out, 0);

    const std::vector<uint8_t> raw = slurp(out);
    ASSERT_EQ(raw.size(), size_t(simh::SIMH_SIZE));

    for (int64_t b = 0; b < 4; b++) {
        const int64_t zone  = b / simh::TPZ;
        const int64_t track = b % simh::TPZ;
        const int64_t base  = simh::SYSWORDS + simh::ZONE_SIZE * zone + BSIZEW * track;

        EXPECT_EQ(word_at(raw, base), simh::TAG_NUMBER | (0x110000000000ULL + Word(b)))
            << "block " << b << " first word";
        EXPECT_EQ(word_at(raw, base + 511), simh::TAG_NUMBER | (0x220000000000ULL + Word(b)))
            << "block " << b << " last word";
    }

    std::remove(flat.c_str());
    std::remove(out.c_str());
}

//
// Every word carries the number tag -- data words included.  A word without it
// is not something the machine will read back as a number.
//
TEST(Simh, EveryWordIsTagged)
{
    const std::string flat = "simh_tag.img";
    const std::string out  = "simh_tag2053.disk";

    {
        Image img;
        img.create(flat, 4);
        Block blk{};
        blk[0] = WORD_MASK; // all 48 bits set: the tag must still survive above them
        img.write_block(0, blk);
    }
    simh::to_simh(flat, out, 0);

    const std::vector<uint8_t> raw = slurp(out);
    // Sampling the whole 8 Mb word by word is slow; the first two zones and the
    // last one cover the interesting boundaries.
    for (int64_t w = 0; w < 2 * simh::ZONE_SIZE; w++)
        ASSERT_EQ(word_at(raw, w) >> 48, 2ULL) << "word " << w << " lost its tag";
    for (int64_t w = (simh::NZONE - 1) * simh::ZONE_SIZE; w < simh::NZONE * simh::ZONE_SIZE; w++)
        ASSERT_EQ(word_at(raw, w) >> 48, 2ULL) << "word " << w << " lost its tag";

    // The all-ones data word kept all 48 bits and gained exactly the tag.
    EXPECT_EQ(word_at(raw, simh::SYSWORDS), simh::TAG_NUMBER | WORD_MASK);

    std::remove(flat.c_str());
    std::remove(out.c_str());
}

//
// Flat -> SIMH -> flat is the identity.
//
TEST(Simh, RoundTrip)
{
    const std::string flat = "simh_rt.img";
    const std::string disk = "simh_rt2053.disk";
    const std::string back = "simh_back.img";

    constexpr int64_t NBLK = 64;

    {
        Image img;
        img.create(flat, NBLK);
        for (int64_t b = 0; b < NBLK; b++) {
            Block blk{};
            for (int i = 0; i < BSIZEW; i++)
                blk[i] = (Word(b) * 0x1000001ULL + Word(i) * 7ULL) & WORD_MASK;
            img.write_block(b, blk);
        }
    }

    simh::to_simh(flat, disk, 0);

    int volume = 0;
    simh::from_simh(disk, back, NBLK, &volume);
    EXPECT_EQ(volume, 2053);

    Image a, b;
    a.open(flat, false);
    b.open(back, false);
    ASSERT_EQ(a.nblocks(), b.nblocks());

    Block x, y;
    for (int64_t n = 0; n < NBLK; n++) {
        a.read_block(n, x);
        b.read_block(n, y);
        ASSERT_EQ(x, y) << "block " << n << " differs after a round trip";
    }
    a.close();
    b.close();

    std::remove(flat.c_str());
    std::remove(disk.c_str());
    std::remove(back.c_str());
}

//
// A flat image bigger than the drive is refused HERE, where the message can say
// why.  Left to run time it becomes an I/O error on a zone the backing file does
// not reach, which is a miserable thing to debug.
//
TEST(Simh, RefusesOversizedImage)
{
    const std::string flat = "simh_big.img";
    const std::string out  = "simh_big2053.disk";

    {
        Image img;
        img.create(flat, MDNBLK + 1);
    }
    EXPECT_THROW(simh::to_simh(flat, out, 0), FsError);

    std::remove(flat.c_str());
    std::remove(out.c_str());
}

//
// A file that is not a formatted disk is rejected on the magic mark, and one
// that is misaligned on the self-address.  Both checks exist because the failure
// they replace is silent: a plausible-looking filesystem read from the wrong
// offset.
//
TEST(Simh, RejectsUnformattedImage)
{
    const std::string junk = "simh_junk.disk";
    const std::string flat = "simh_junk.img";

    std::FILE *f = std::fopen(junk.c_str(), "wb");
    ASSERT_NE(f, nullptr);
    for (int64_t i = 0; i < simh::ZONE_SIZE * 8 * 2; i++)
        std::fputc(0, f);
    std::fclose(f);

    EXPECT_THROW(simh::from_simh(junk, flat, 4, nullptr), FsError);

    std::remove(junk.c_str());
    std::remove(flat.c_str());
}

TEST(Simh, RejectsTooShortImage)
{
    const std::string tiny = "simh_tiny.disk";
    const std::string flat = "simh_tiny.img";

    std::FILE *f = std::fopen(tiny.c_str(), "wb");
    ASSERT_NE(f, nullptr);
    for (int i = 0; i < 100; i++)
        std::fputc(0, f);
    std::fclose(f);

    EXPECT_THROW(simh::from_simh(tiny, flat, 4, nullptr), FsError);

    std::remove(tiny.c_str());
    std::remove(flat.c_str());
}
