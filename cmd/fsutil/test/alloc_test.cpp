//
// The free list.
//
// AllocOrderIsAscending is the one that matters.  Everything else here checks a
// mechanism; that one checks the CONSEQUENCE the mechanism exists for -- that a
// free list built the way create.cpp builds it hands the kernel blocks in
// increasing order, across the chain-block boundary, without a gap.  Get the build
// order backwards and every test below still passes except that one.
//
#include <gtest/gtest.h>

#include <cstdio>
#include <set>

#include "filesystem.h"

namespace {

constexpr int64_t NBLK  = 2000;
constexpr int64_t ISIZE = 34; // first data block, as create.cpp computes it

//
// A filesystem with a free list built the way create.cpp builds it: descending,
// from the last block down to the first data block, with the superblock's cache
// starting empty so that block_free() plants its own sentinel.
//
void build(Filesystem &fs, const char *path, int64_t nblk = NBLK, int64_t isize = ISIZE)
{
    fs.image.create(path, nblk);
    fs.sb.isize = isize;
    fs.sb.fsize = nblk;
    fs.sb.nfree = 0;
    fs.sb.tfree = 0;

    for (int64_t n = nblk - 1; n >= isize; n--)
        fs.block_free(n);
}

} // namespace

//
// THE test: the kernel gets ascending blocks, and keeps getting them across the
// point where the cache spills into a chain block.
//
TEST(Alloc, AllocOrderIsAscending)
{
    const char *path = "alloc_asc.img";
    Filesystem fs;
    build(fs, path);

    // Well past the first chain boundary at NICFREE == 320.
    for (int64_t expect = ISIZE; expect < ISIZE + 400; expect++)
        ASSERT_EQ(fs.block_alloc(), expect) << "allocation " << (expect - ISIZE);

    fs.image.close();
    std::remove(path);
}

//
// Every data block comes out exactly once, and they are all there.
//
TEST(Alloc, EveryBlockOnceAndOnly)
{
    const char *path = "alloc_all.img";
    Filesystem fs;
    build(fs, path);

    std::set<int64_t> seen;
    int64_t n    = 0;
    int64_t prev = ISIZE - 1;
    for (;; n++) {
        int64_t bno;
        try {
            bno = fs.block_alloc();
        } catch (const FsError &) {
            break;
        }
        ASSERT_GE(bno, ISIZE) << "handed out an i-list block";
        ASSERT_LT(bno, NBLK) << "handed out a block past the volume";
        ASSERT_TRUE(seen.insert(bno).second) << "block " << bno << " handed out twice";

        //
        // Ascending across the WHOLE volume, not just the first cache-full.  Every
        // chain-block reload is a chance for the order to break, and there are six
        // of them on a 2000-block drive.
        //
        ASSERT_EQ(bno, prev + 1) << "allocation " << n << " broke the ascending run";
        prev = bno;
    }

    EXPECT_EQ(n, NBLK - ISIZE) << "the volume has " << (NBLK - ISIZE) << " data blocks";
    EXPECT_EQ(seen.size(), size_t(NBLK - ISIZE));

    fs.image.close();
    std::remove(path);
}

//
// The chain block's own layout: a count, NICFREE addresses, and 191 words of
// zero.  The kernel clrbuf()s before filling it (alloc.c:104-110) because without
// that the tail went to disk as stale memory; an image written here must match.
//
TEST(Alloc, ChainBlockLayout)
{
    const char *path = "alloc_chain.img";
    Filesystem fs;
    build(fs, path);

    //
    // Where the first spill lands, and why it is not where you would guess.
    //
    // THE SENTINEL COSTS A SLOT.  The first block_free() plants a 0 at s_free[0]
    // before storing anything, so the cache holds only NICFREE-1 == 319 real
    // blocks before it is full.  Freeing descending from 1999, slots 1..319 take
    // 1999 down to 1681, and the next call -- free(1680) -- is the one that
    // spills.  So the first chain block is NBLK - NICFREE, one higher than the
    // NBLK - 1 - NICFREE this test originally guessed.
    //
    const int64_t chain_bno = NBLK - NICFREE;
    EXPECT_EQ(chain_bno, 1680);

    Block chain;
    fs.image.read_block(chain_bno, chain);

    EXPECT_EQ(from_word(chain[FB_NFREE]), NICFREE);

    // Slot 0 is the sentinel; slots 1..319 are the blocks freed before the spill.
    EXPECT_EQ(from_word(chain[FB_FREE]), 0) << "df_free[0] is the end-of-list sentinel";
    for (int i = 1; i < NICFREE; i++)
        EXPECT_EQ(from_word(chain[FB_FREE + i]), NBLK - i) << "df_free[" << i << "]";

    // And the tail is zero, all 191 words of it.
    for (int i = FB_FREE + NICFREE; i < BSIZEW; i++)
        EXPECT_EQ(chain[i], 0ULL) << "chain block word " << i << " must be zero";

    fs.image.close();
    std::remove(path);
}

//
// The invariant that looks like a bug: a spilled block is both the chain block
// and s_free[0], so it gets handed out again -- by the very allocation that
// drains the cache it holds.
//
TEST(Alloc, SpilledBlockIsAlsoAllocated)
{
    const char *path = "alloc_spill.img";
    Filesystem fs;
    build(fs, path);

    const int64_t chain_bno = NBLK - NICFREE;

    //
    // Allocation runs ascending from the first data block, so reaching 1680 means
    // draining nearly the whole volume.  That is the point: the chain block is not
    // reserved, it is handed out in its turn like any other, and the allocation
    // that hands it out is the one that reads the next NICFREE addresses back out
    // of it first.
    //
    std::set<int64_t> seen;
    for (;;) {
        try {
            seen.insert(fs.block_alloc());
        } catch (const FsError &) {
            break;
        }
    }

    EXPECT_TRUE(seen.count(chain_bno))
        << "the chain block " << chain_bno << " must be handed out as a data block too";

    // And the blocks it pointed at came out as well -- proof the reload worked.
    EXPECT_TRUE(seen.count(NBLK - 1));
    EXPECT_TRUE(seen.count(chain_bno + 1));

    fs.image.close();
    std::remove(path);
}

//
// Exhaustion stops on the sentinel and reports, rather than running s_nfree
// negative or handing out block 0.
//
TEST(Alloc, ExhaustionIsClean)
{
    const char *path = "alloc_end.img";
    Filesystem fs;

    // A small volume, so the whole list fits inside the superblock's cache.
    build(fs, path, 100, 34);

    for (int64_t i = 0; i < 100 - 34; i++)
        ASSERT_NO_THROW(fs.block_alloc()) << "allocation " << i;

    EXPECT_THROW(fs.block_alloc(), FsError);
    EXPECT_GE(fs.sb.nfree, 0) << "s_nfree must not go negative";

    fs.image.close();
    std::remove(path);
}

//
// The sentinel: block_free() plants a 0 at s_free[0] on a fresh list, so it sits
// at the bottom of the first chain block and is the last thing ever popped.
//
TEST(Alloc, SentinelIsPlantedOnFirstFree)
{
    const char *path = "alloc_sentinel.img";
    Filesystem fs;

    fs.image.create(path, NBLK);
    fs.sb.isize = ISIZE;
    fs.sb.fsize = NBLK;
    fs.sb.nfree = 0;

    fs.block_free(1999);

    EXPECT_EQ(fs.sb.nfree, 2);
    EXPECT_EQ(fs.sb.free[0], 0) << "the end-of-list sentinel";
    EXPECT_EQ(fs.sb.free[1], 1999);

    fs.image.close();
    std::remove(path);
}

//
// LIFO: a block freed mid-stream is the next one out.
//
TEST(Alloc, FreeThenAllocIsLifo)
{
    const char *path = "alloc_lifo.img";
    Filesystem fs;
    build(fs, path);

    for (int i = 0; i < 10; i++)
        fs.block_alloc();

    const int64_t victim = 1234;
    fs.block_free(victim);
    EXPECT_EQ(fs.block_alloc(), victim);

    fs.image.close();
    std::remove(path);
}

//
// badblock(): the i-list and anything past the volume are not data blocks.
// Freeing one is refused rather than quietly corrupting the i-list.
//
TEST(Alloc, RefusesNonDataBlocks)
{
    const char *path = "alloc_bad.img";
    Filesystem fs;
    build(fs, path);

    EXPECT_THROW(fs.block_free(0), FsError);         // boot block
    EXPECT_THROW(fs.block_free(SUPERB), FsError);    // superblock
    EXPECT_THROW(fs.block_free(ISIZE - 1), FsError); // last i-list block
    EXPECT_THROW(fs.block_free(NBLK), FsError);      // past the end
    EXPECT_NO_THROW(fs.block_free(ISIZE));           // the first data block is fine

    fs.image.close();
    std::remove(path);
}

//
// A freshly allocated block reads as zeroes, matching the kernel's clrbuf().
//
TEST(Alloc, AllocatedBlockIsCleared)
{
    const char *path = "alloc_clr.img";
    Filesystem fs;
    build(fs, path);

    // Put something in a block, free it, take it back, and check it came back clean.
    const int64_t bno = fs.block_alloc();
    Block dirt{};
    dirt[0]   = WORD_MASK;
    dirt[511] = WORD_MASK;
    fs.image.write_block(bno, dirt);

    fs.block_free(bno);
    EXPECT_EQ(fs.block_alloc(), bno);

    Block back;
    fs.image.read_block(bno, back);
    for (int i = 0; i < BSIZEW; i++)
        ASSERT_EQ(back[i], 0ULL) << "word " << i << " was not cleared";

    fs.image.close();
    std::remove(path);
}

//
// The free count tracks the list.  This is the tool's own bookkeeping -- the
// kernel does not maintain s_tfree -- but fsck and `-v' report it, so it should
// at least be self-consistent.
//
TEST(Alloc, FreeCountTracksTheList)
{
    const char *path = "alloc_count.img";
    Filesystem fs;
    build(fs, path);

    EXPECT_EQ(fs.sb.tfree, NBLK - ISIZE);

    for (int i = 0; i < 50; i++)
        fs.block_alloc();
    EXPECT_EQ(fs.sb.tfree, NBLK - ISIZE - 50);

    fs.image.close();
    std::remove(path);
}
