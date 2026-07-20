//
// The inode: where it lives in the i-list, what its words mean, and how a logical
// block number becomes a physical one.
//
// BmapAgainstKernelModel is the important one.  It does not check bmap() against
// a table of expected answers -- a table encodes my understanding of subr.c, and
// if that understanding is wrong the table is wrong in the same direction.  It
// checks bmap() against a SECOND transcription of kernel/subr.c written to look
// like the kernel rather than like this tool, so the two can only agree by both
// being right.
//
#include "inode.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <vector>

namespace {

constexpr int64_t NBLK  = 2000;
constexpr int64_t ISIZE = 34;

void build(Filesystem &fs, const char *path)
{
    fs.image.create(path, NBLK);
    fs.sb.isize = ISIZE;
    fs.sb.fsize = NBLK;
    fs.sb.nfree = 0;
    fs.sb.tfree = 0;
    for (int64_t n = NBLK - 1; n >= ISIZE; n--)
        fs.block_free(n);
}

//
// kernel/subr.c:24-124, transcribed a second time -- deliberately in the kernel's
// own shape, with its variable names and its two loops, reading the address array
// through a plain pointer.  It answers only "which slot and which indices", which
// is the part of bmap() that is easy to get wrong.
//
struct Walk {
    bool direct  = false;
    int slot     = -1; // index into i_addr
    int nlevels  = 0;  // indirect blocks to walk
    int index[2] = { -1, -1 };
    bool too_big = false;
};

Walk kernel_bmap(int64_t bn)
{
    Walk w;

    if (bn < NADDR - NLEVEL) {
        w.direct = true;
        w.slot   = int(bn);
        return w;
    }

    int sh     = 0;
    int64_t nb = 1;
    int j;
    bn -= NADDR - NLEVEL;
    for (j = NLEVEL; j > 0; j--) {
        sh += NSHIFT;
        nb <<= NSHIFT;
        if (bn < nb)
            break;
        bn -= nb;
    }
    if (j == 0) {
        w.too_big = true;
        return w;
    }

    w.slot    = NADDR - j;
    w.nlevels = NLEVEL - j + 1;

    int k = 0;
    for (; j <= NLEVEL; j++) {
        sh -= NSHIFT;
        w.index[k++] = int((bn >> sh) & NMASK);
    }
    return w;
}

} // namespace

//
// Inode 1 is block 2 slot 0: the i-list starts after the boot block and the
// superblock, which is what v7's `2*INOPB - 1' bias buys.
//
TEST(Inode, ItodItoo)
{
    const struct {
        int64_t ino;
        int64_t blk;
        int slot;
    } cases[] = {
        { 1, 2, 0 },   { 2, 2, 1 },  { 31, 2, 30 },    { 32, 2, 31 },   { 33, 3, 0 },
        { 64, 3, 31 }, { 65, 4, 0 }, { 1024, 33, 31 }, { 1025, 34, 0 },
    };

    for (const auto &c : cases) {
        EXPECT_EQ(itod(c.ino), c.blk) << "inode " << c.ino;
        EXPECT_EQ(itoo(c.ino), c.slot) << "inode " << c.ino;
    }
}

//
// The i-list is dense: consecutive inodes are consecutive slots, INOPB to a
// block, with nothing skipped and nothing straddling.
//
TEST(Inode, IlistIsDense)
{
    for (int64_t ino = 1; ino < 2000; ino++) {
        const int64_t here = itod(ino) * INOPB + itoo(ino);
        const int64_t next = itod(ino + 1) * INOPB + itoo(ino + 1);
        ASSERT_EQ(next, here + 1) << "inode " << ino << " and " << ino + 1 << " are not adjacent";
    }
}

//
// Every word of the dinode, at the offset ino.h gives it.  Read back raw, not
// through get().
//
TEST(Inode, FieldOffsets)
{
    const char *path = "ino_offsets.img";
    Filesystem fs;
    build(fs, path);

    Inode ip;
    ip.get(fs, 5);
    ip.mode  = 0x111;
    ip.nlink = 0x222;
    ip.uid   = 0x333;
    ip.gid   = 0x444;
    ip.size  = 0x555;
    ip.atime = 0x666;
    ip.mtime = 0x777;
    ip.ctime = 0x888;
    for (int i = 0; i < NADDR; i++)
        ip.addr[i] = 1000 + i;
    ip.save(true);

    Block b;
    fs.image.read_block(itod(5), b);
    const size_t base = size_t(itoo(5)) * DI_WORDS;

    EXPECT_EQ(from_word(b[base + 0]), 0x111);
    EXPECT_EQ(from_word(b[base + 1]), 0x222);
    EXPECT_EQ(from_word(b[base + 2]), 0x333);
    EXPECT_EQ(from_word(b[base + 3]), 0x444);
    EXPECT_EQ(from_word(b[base + 4]), 0x555);
    EXPECT_EQ(from_word(b[base + 5]), 0x666);
    EXPECT_EQ(from_word(b[base + 6]), 0x777);
    EXPECT_EQ(from_word(b[base + 7]), 0x888);
    for (int i = 0; i < NADDR; i++)
        EXPECT_EQ(from_word(b[base + 8 + i]), 1000 + i) << "di_addr[" << i << "]";

    fs.image.close();
    std::remove(path);
}

//
// Saving one inode must not disturb the other 31 sharing its block.
//
TEST(Inode, SaveDoesNotDisturbNeighbours)
{
    const char *path = "ino_neigh.img";
    Filesystem fs;
    build(fs, path);

    // Inodes 3, 4 and 5 all live in block 2.
    for (int64_t ino = 3; ino <= 5; ino++) {
        Inode ip;
        ip.get(fs, ino);
        ip.mode = 0100000 + ino;
        ip.size = ino * 100;
        ip.save(true);
    }

    for (int64_t ino = 3; ino <= 5; ino++) {
        Inode ip;
        ip.get(fs, ino);
        EXPECT_EQ(ip.mode, 0100000 + ino) << "inode " << ino;
        EXPECT_EQ(ip.size, ino * 100) << "inode " << ino;
    }

    fs.image.close();
    std::remove(path);
}

//
// THE bmap test: against a second, independent transcription of subr.c.
//
TEST(Inode, BmapAgainstKernelModel)
{
    const char *path = "ino_bmap.img";
    Filesystem fs;
    build(fs, path);

    Inode ip;
    ip.get(fs, 2);

    //
    // Interesting block numbers: every direct slot, both sides of the
    // direct/single boundary, both ends of the single indirect, and both sides of
    // the single/double boundary.
    //
    const int64_t lbns[] = { 0, 1, 5, 6, 7, 511, 516, 517, 518, 519, 1029, 1030, 1541 };

    for (int64_t lbn : lbns) {
        const Walk w = kernel_bmap(lbn);
        ASSERT_FALSE(w.too_big) << "lbn " << lbn;

        const int64_t phys = ip.bmap(lbn, true);
        ASSERT_GT(phys, 0) << "lbn " << lbn;

        if (w.direct) {
            EXPECT_EQ(ip.addr[size_t(w.slot)], phys)
                << "lbn " << lbn << " should be direct slot " << w.slot;
            continue;
        }

        //
        // Follow the chain by hand, using the model's slot and indices, and check
        // we arrive at the same physical block bmap() returned.
        //
        int64_t nb = ip.addr[size_t(w.slot)];
        ASSERT_GT(nb, 0) << "lbn " << lbn << " slot " << w.slot;

        for (int k = 0; k < w.nlevels; k++) {
            Block ind;
            fs.image.read_block(nb, ind);
            nb = from_word(ind[size_t(w.index[k])]);
            ASSERT_GT(nb, 0) << "lbn " << lbn << " level " << k;
        }
        EXPECT_EQ(nb, phys) << "lbn " << lbn << " walked to a different block";
    }

    fs.image.close();
    std::remove(path);
}

//
// The boundaries, stated outright so a regression names itself.
//
TEST(Inode, IndirectionBoundaries)
{
    // 0..5 direct.
    EXPECT_TRUE(kernel_bmap(5).direct);
    EXPECT_FALSE(kernel_bmap(6).direct);

    // 6..517 through the single indirect, addr[6].
    EXPECT_EQ(kernel_bmap(6).slot, NADDR - 2);
    EXPECT_EQ(kernel_bmap(6).nlevels, 1);
    EXPECT_EQ(kernel_bmap(6).index[0], 0);
    EXPECT_EQ(kernel_bmap(517).slot, NADDR - 2);
    EXPECT_EQ(kernel_bmap(517).index[0], NINDIR - 1);

    // 518 onward through the double indirect, addr[7].
    EXPECT_EQ(kernel_bmap(518).slot, NADDR - 1);
    EXPECT_EQ(kernel_bmap(518).nlevels, 2);
    EXPECT_EQ(kernel_bmap(518).index[0], 0);
    EXPECT_EQ(kernel_bmap(518).index[1], 0);

    // One full single-indirect block further along the double.
    EXPECT_EQ(kernel_bmap(518 + NINDIR).index[0], 1);
    EXPECT_EQ(kernel_bmap(518 + NINDIR).index[1], 0);

    // And there is no third level: past the double is EFBIG, not a triple.
    EXPECT_TRUE(kernel_bmap(518 + int64_t(NINDIR) * NINDIR).too_big);
}

//
// A block that is not there reads as a hole rather than being allocated.
//
TEST(Inode, BmapReadDoesNotAllocate)
{
    const char *path = "ino_hole.img";
    Filesystem fs;
    build(fs, path);

    Inode ip;
    ip.get(fs, 2);

    EXPECT_EQ(ip.bmap(0, false), -1);
    EXPECT_EQ(ip.bmap(600, false), -1);
    EXPECT_EQ(ip.addr[0], 0) << "a read must not have allocated anything";
    EXPECT_EQ(ip.addr[NADDR - 1], 0);

    fs.image.close();
    std::remove(path);
}

TEST(Inode, BmapRejectsTooLarge)
{
    const char *path = "ino_big.img";
    Filesystem fs;
    build(fs, path);

    Inode ip;
    ip.get(fs, 2);
    EXPECT_THROW(ip.bmap(518 + int64_t(NINDIR) * NINDIR, false), FsError);
    EXPECT_THROW(ip.bmap(-1, false), FsError);

    fs.image.close();
    std::remove(path);
}

//
// di_size is in BYTES.  A file of 3073 bytes is two blocks, and the size on disk
// is 3073 -- not 2, not 6144, not 513.
//
TEST(Inode, SizeIsBytes)
{
    const char *path = "ino_size.img";
    Filesystem fs;
    build(fs, path);

    Inode ip;
    ip.get(fs, 2);
    ip.mode = IFREG | 0644;

    std::vector<uint8_t> data(3073);
    for (size_t i = 0; i < data.size(); i++)
        data[i] = uint8_t(i);

    ip.write(0, data.data(), int64_t(data.size()));

    EXPECT_EQ(ip.size, 3073);
    EXPECT_GT(ip.addr[0], 0) << "block 0 is mapped";
    EXPECT_GT(ip.addr[1], 0) << "block 1 is mapped";
    EXPECT_EQ(ip.addr[2], 0) << "and nothing beyond it";

    fs.image.close();
    std::remove(path);
}

//
// Write and read back, spanning the direct blocks, the single indirect and into
// the double.  600 blocks is 1.8 Mb -- past the 518-block single indirect, so the
// double is exercised for real and not just by arithmetic.
//
TEST(Inode, LargeFileRoundTrip)
{
    const char *path = "ino_large.img";
    Filesystem fs;
    build(fs, path);

    Inode ip;
    ip.get(fs, 2);
    ip.mode = IFREG | 0644;

    //
    // A recognisable pattern per block, so a misplaced block shows up as a wrong
    // value rather than as zeroes.
    //
    constexpr int64_t NB = 600;
    std::vector<uint8_t> block(BSIZE);

    for (int64_t b = 0; b < NB; b++) {
        for (int64_t i = 0; i < BSIZE; i++)
            block[size_t(i)] = uint8_t(b * 7 + i);
        ip.write(b * BSIZE, block.data(), BSIZE);
    }
    EXPECT_EQ(ip.size, NB * BSIZE);

    std::vector<uint8_t> back(BSIZE);
    for (int64_t b = 0; b < NB; b++) {
        ASSERT_EQ(ip.read(b * BSIZE, back.data(), BSIZE), BSIZE) << "block " << b;
        for (int64_t i = 0; i < BSIZE; i++)
            ASSERT_EQ(back[size_t(i)], uint8_t(b * 7 + i)) << "block " << b << " byte " << i;
    }

    // The double indirect really was used.
    EXPECT_GT(ip.addr[NADDR - 1], 0) << "a 600-block file must reach the double indirect";

    fs.image.close();
    std::remove(path);
}

//
// A write that does not start or end on a block boundary leaves its neighbours
// alone.
//
TEST(Inode, UnalignedWrite)
{
    const char *path = "ino_unalign.img";
    Filesystem fs;
    build(fs, path);

    Inode ip;
    ip.get(fs, 2);
    ip.mode = IFREG | 0644;

    std::vector<uint8_t> filler(BSIZE * 3, 0xAA);
    ip.write(0, filler.data(), BSIZE * 3);

    const uint8_t patch[] = { 1, 2, 3, 4, 5 };
    ip.write(BSIZE - 2, patch, 5); // straddles the block boundary

    std::vector<uint8_t> back(BSIZE * 3);
    ASSERT_EQ(ip.read(0, back.data(), BSIZE * 3), BSIZE * 3);

    EXPECT_EQ(back[BSIZE - 3], 0xAA) << "the byte before the patch";
    EXPECT_EQ(back[BSIZE - 2], 1);
    EXPECT_EQ(back[BSIZE - 1], 2);
    EXPECT_EQ(back[BSIZE + 0], 3);
    EXPECT_EQ(back[BSIZE + 1], 4);
    EXPECT_EQ(back[BSIZE + 2], 5);
    EXPECT_EQ(back[BSIZE + 3], 0xAA) << "the byte after the patch";

    fs.image.close();
    std::remove(path);
}

//
// A hole reads as zeroes, as it does through the kernel.
//
TEST(Inode, HolesReadAsZero)
{
    const char *path = "ino_sparse.img";
    Filesystem fs;
    build(fs, path);

    Inode ip;
    ip.get(fs, 2);
    ip.mode = IFREG | 0644;

    const uint8_t byte = 0x5A;
    ip.write(BSIZE * 4, &byte, 1); // leaves blocks 0..3 unmapped

    EXPECT_EQ(ip.size, BSIZE * 4 + 1);
    EXPECT_EQ(ip.addr[0], 0) << "block 0 must still be a hole";

    std::vector<uint8_t> back(BSIZE);
    ASSERT_EQ(ip.read(0, back.data(), BSIZE), BSIZE);
    for (int64_t i = 0; i < BSIZE; i++)
        ASSERT_EQ(back[size_t(i)], 0) << "hole byte " << i;

    fs.image.close();
    std::remove(path);
}

//
// read() stops at end of file rather than running past it.
//
TEST(Inode, ReadStopsAtEof)
{
    const char *path = "ino_eof.img";
    Filesystem fs;
    build(fs, path);

    Inode ip;
    ip.get(fs, 2);
    ip.mode = IFREG | 0644;

    const uint8_t data[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    ip.write(0, data, 10);

    uint8_t back[100];
    EXPECT_EQ(ip.read(0, back, 100), 10);
    EXPECT_EQ(ip.read(5, back, 100), 5);
    EXPECT_EQ(ip.read(10, back, 100), 0);
    EXPECT_EQ(ip.read(999, back, 100), 0);

    fs.image.close();
    std::remove(path);
}

//
// truncate() gives every block back -- direct, single indirect and double, plus
// the indirect blocks themselves.
//
TEST(Inode, TruncateReturnsEveryBlock)
{
    const char *path = "ino_trunc.img";
    Filesystem fs;
    build(fs, path);

    const int64_t before = fs.sb.tfree;

    Inode ip;
    ip.get(fs, 2);
    ip.mode = IFREG | 0644;

    // 600 blocks: direct, the whole single indirect, and into the double.
    std::vector<uint8_t> block(BSIZE, 0x77);
    for (int64_t b = 0; b < 600; b++)
        ip.write(b * BSIZE, block.data(), BSIZE);

    EXPECT_LT(fs.sb.tfree, before);

    ip.truncate();

    EXPECT_EQ(ip.size, 0);
    for (int i = 0; i < NADDR; i++)
        EXPECT_EQ(ip.addr[i], 0) << "di_addr[" << i << "] must be released";

    EXPECT_EQ(fs.sb.tfree, before) << "every block, including the indirect blocks, must come back";

    fs.image.close();
    std::remove(path);
}
