//
// The superblock's word offsets are the ABI between this tool and the kernel.
//
// FieldOffsets below is the whole contract in one test: a distinct sentinel goes
// into every field, the block is written, and the RAW words are checked at the
// offsets include/sys/filsys.h names.  It reads the block back through Image
// rather than through SuperBlock::load(), so a load/save pair of matching bugs
// cannot pass it.
//
#include "superblock.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <sstream>

namespace {

//
// A superblock that would mount: geometry right, sizes sane, counts in range.
// Each test then breaks exactly one thing.
//
SuperBlock good()
{
    SuperBlock sb;
    sb.isize  = 34;
    sb.fsize  = 2000;
    sb.time   = 1000000;
    sb.tfree  = 1965;
    sb.tinode = 1023;
    sb.nfree  = 46;
    sb.ninode = 160;
    return sb;
}

} // namespace

//
// Every field, at the offset filsys.h gives it.
//
TEST(SuperBlock, FieldOffsets)
{
    const char *path = "sb_offsets.img";

    SuperBlock sb;
    sb.magic  = int64_t(FS_MAGIC);
    sb.bsize  = BSIZEW;
    sb.inopb  = INOPB;
    sb.naddr  = NADDR;
    sb.isize  = 0x111;
    sb.fsize  = 0x222;
    sb.time   = 0x333;
    sb.tfree  = 0x444;
    sb.tinode = 0x555;
    sb.flock  = 0x666;
    sb.ilock  = 0x777;
    sb.fmod   = 0x888;
    sb.ronly  = 0x999;
    sb.nfree  = 0xAAA;
    sb.ninode = 0xBBB;
    for (int i = 0; i < NICFREE; i++)
        sb.free[i] = 10000 + i;
    for (int i = 0; i < NICINOD; i++)
        sb.inode[i] = 20000 + i;

    Image img;
    img.create(path, 3);
    sb.save(img);

    // Read the block back raw -- NOT through SuperBlock::load().
    Block b;
    img.read_block(SUPERB, b);

    EXPECT_EQ(from_word(b[0]), int64_t(FS_MAGIC));
    EXPECT_EQ(from_word(b[1]), BSIZEW);
    EXPECT_EQ(from_word(b[2]), INOPB);
    EXPECT_EQ(from_word(b[3]), NADDR);
    EXPECT_EQ(from_word(b[4]), 0x111);
    EXPECT_EQ(from_word(b[5]), 0x222);
    EXPECT_EQ(from_word(b[6]), 0x333);
    EXPECT_EQ(from_word(b[7]), 0x444);
    EXPECT_EQ(from_word(b[8]), 0x555);
    EXPECT_EQ(from_word(b[9]), 0x666);
    EXPECT_EQ(from_word(b[10]), 0x777);
    EXPECT_EQ(from_word(b[11]), 0x888);
    EXPECT_EQ(from_word(b[12]), 0x999);

    EXPECT_EQ(from_word(b[13]), 0xAAA);        // s_nfree
    EXPECT_EQ(from_word(b[14]), 10000);        // s_free[0]
    EXPECT_EQ(from_word(b[333]), 10000 + 319); // s_free[NICFREE-1]
    EXPECT_EQ(from_word(b[334]), 0xBBB);       // s_ninode
    EXPECT_EQ(from_word(b[335]), 20000);       // s_inode[0]
    EXPECT_EQ(from_word(b[494]), 20000 + 159); // s_inode[NICINOD-1]

    // s_fill[17]: written as zero, all the way to the end of the block.
    for (int i = 495; i < BSIZEW; i++)
        EXPECT_EQ(b[i], 0ULL) << "s_fill word " << i << " must be zero";

    img.close();
    std::remove(path);
}

//
// The offsets named in superblock.h are the ones the test above hardcodes.  If
// someone retunes NICFREE, both move together and this catches the mismatch.
//
TEST(SuperBlock, NamedOffsetsMatchLayout)
{
    EXPECT_EQ(SB_NFREE, 13);
    EXPECT_EQ(SB_FREE, 14);
    EXPECT_EQ(SB_FREE + NICFREE - 1, 333);
    EXPECT_EQ(SB_NINODE, 334);
    EXPECT_EQ(SB_INODE, 335);
    EXPECT_EQ(SB_INODE + NICINOD - 1, 494);
    EXPECT_EQ(SB_FILL, 495);
    EXPECT_EQ(SB_FILL + 17, BSIZEW);
}

TEST(SuperBlock, SaveLoadRoundTrip)
{
    const char *path = "sb_rt.img";

    SuperBlock out = good();
    for (int i = 0; i < NICFREE; i++)
        out.free[i] = 100 + i;
    for (int i = 0; i < NICINOD; i++)
        out.inode[i] = 200 + i;

    Image img;
    img.create(path, 3);
    out.save(img);

    SuperBlock in;
    in.load(img);

    EXPECT_EQ(in.magic, out.magic);
    EXPECT_EQ(in.isize, out.isize);
    EXPECT_EQ(in.fsize, out.fsize);
    EXPECT_EQ(in.time, out.time);
    EXPECT_EQ(in.tfree, out.tfree);
    EXPECT_EQ(in.tinode, out.tinode);
    EXPECT_EQ(in.nfree, out.nfree);
    EXPECT_EQ(in.ninode, out.ninode);
    EXPECT_EQ(in.free, out.free);
    EXPECT_EQ(in.inode, out.inode);

    img.close();
    std::remove(path);
}

//
// sbcheck() accepts a well-formed superblock ...
//
TEST(SuperBlock, ValidateAccepts)
{
    std::ostringstream err;
    EXPECT_TRUE(good().validate(err)) << err.str();
    EXPECT_TRUE(err.str().empty());
}

//
// ... and rejects each of its clauses in turn.  One case per `if' in
// kernel/alloc.c:156-180; the message is the kernel's own.
//
TEST(SuperBlock, ValidateRejectsBadMagic)
{
    SuperBlock sb = good();
    sb.magic      = 0;
    std::ostringstream err;
    EXPECT_FALSE(sb.validate(err));
    EXPECT_EQ(err.str(), "not a filesystem\n");
}

TEST(SuperBlock, ValidateRejectsGeometry)
{
    for (const char *what : { "bsize", "inopb", "naddr" }) {
        SuperBlock sb = good();
        if (what[0] == 'b')
            sb.bsize = 256; // a 512-BYTE block, the v7 value
        else if (what[0] == 'i')
            sb.inopb = 8; // what INOPB was before commit bedbdf2
        else
            sb.naddr = 13; // what NADDR was before commit bedbdf2

        std::ostringstream err;
        EXPECT_FALSE(sb.validate(err)) << what;
        EXPECT_EQ(err.str(), "filesystem geometry mismatch\n") << what;
    }
}

TEST(SuperBlock, ValidateRejectsBadSize)
{
    // s_isize must be strictly past the superblock ...
    {
        SuperBlock sb = good();
        sb.isize      = SUPERB;
        std::ostringstream err;
        EXPECT_FALSE(sb.validate(err));
        EXPECT_EQ(err.str(), "bad filesystem size\n");
    }
    // ... and strictly before the end of the volume.
    {
        SuperBlock sb = good();
        sb.isize      = sb.fsize;
        std::ostringstream err;
        EXPECT_FALSE(sb.validate(err));
        EXPECT_EQ(err.str(), "bad filesystem size\n");
    }
    {
        SuperBlock sb = good();
        sb.isize      = sb.fsize + 1;
        std::ostringstream err;
        EXPECT_FALSE(sb.validate(err));
        EXPECT_EQ(err.str(), "bad filesystem size\n");
    }
}

TEST(SuperBlock, ValidateRejectsBadCounts)
{
    const int64_t bad_nfree[]  = { -1, NICFREE + 1 };
    const int64_t bad_ninode[] = { -1, NICINOD + 1 };

    for (int64_t n : bad_nfree) {
        SuperBlock sb = good();
        sb.nfree      = n;
        std::ostringstream err;
        EXPECT_FALSE(sb.validate(err)) << "nfree " << n;
        EXPECT_EQ(err.str(), "bad free count\n");
    }
    for (int64_t n : bad_ninode) {
        SuperBlock sb = good();
        sb.ninode     = n;
        std::ostringstream err;
        EXPECT_FALSE(sb.validate(err)) << "ninode " << n;
        EXPECT_EQ(err.str(), "bad free count\n");
    }
}

//
// The boundaries themselves are legal -- a full cache is not a corrupt one.
//
TEST(SuperBlock, ValidateAcceptsFullCaches)
{
    SuperBlock sb = good();
    sb.nfree      = NICFREE;
    sb.ninode     = NICINOD;
    std::ostringstream err;
    EXPECT_TRUE(sb.validate(err)) << err.str();

    sb.nfree  = 0;
    sb.ninode = 0;
    std::ostringstream err2;
    EXPECT_TRUE(sb.validate(err2)) << err2.str();
}
