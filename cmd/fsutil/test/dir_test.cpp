//
// Directories: the four-word entry, the entry-number arithmetic, and the two
// behaviours a v7 directory has that a BSD one does not -- silent name truncation
// and a size that is not rounded to a block.
//
#include "dir.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <string>
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
// A directory inode with `.' and `..' already in it.
//
void make_dir(Filesystem &fs, Inode &dp, int64_t ino, int64_t parent)
{
    dp.get(fs, ino);
    dp.clear();
    dp.mode  = IFDIR | 0755;
    dp.nlink = 2;
    dir::make_empty(dp, parent);
}

} // namespace

//
// The entry is four words: one of i-number, three of name.  Checked raw.
//
TEST(Dir, EntryLayout)
{
    const char *path = "dir_layout.img";
    Filesystem fs;
    build(fs, path);

    Inode dp;
    make_dir(fs, dp, 2, 2);
    dir::enter(dp, "abcdefghijklmnopqr", 42); // exactly DIRSIZ characters

    Block b;
    fs.image.read_block(dp.addr[0], b);

    // Slot 2, just past `.' and `..'.
    const size_t base = 2 * DIRWORDS;
    EXPECT_EQ(from_word(b[base + DE_INO]), 42);
    EXPECT_EQ(b[base + DE_NAME + 0], 0x616263646566ULL); // "abcdef"
    EXPECT_EQ(b[base + DE_NAME + 1], 0x6768696A6B6CULL); // "ghijkl"
    EXPECT_EQ(b[base + DE_NAME + 2], 0x6D6E6F707172ULL); // "mnopqr"

    fs.image.close();
    std::remove(path);
}

//
// `.' and `..' as they land on disk: byte 0 in bits 48-41, the rest NUL.
//
TEST(Dir, DotAndDotDot)
{
    const char *path = "dir_dot.img";
    Filesystem fs;
    build(fs, path);

    Inode dp;
    make_dir(fs, dp, 2, 2);

    Block b;
    fs.image.read_block(dp.addr[0], b);

    EXPECT_EQ(from_word(b[0 * DIRWORDS + DE_INO]), 2);
    EXPECT_EQ(b[0 * DIRWORDS + DE_NAME + 0], 0x2E0000000000ULL); // "."
    EXPECT_EQ(b[0 * DIRWORDS + DE_NAME + 1], 0ULL);
    EXPECT_EQ(b[0 * DIRWORDS + DE_NAME + 2], 0ULL);

    EXPECT_EQ(from_word(b[1 * DIRWORDS + DE_INO]), 2);
    EXPECT_EQ(b[1 * DIRWORDS + DE_NAME + 0], 0x2E2E00000000ULL); // ".."

    fs.image.close();
    std::remove(path);
}

//
// THE size test.  An empty directory is 48 bytes, not 3072.  Rounding to a block
// -- which the BSD source does in three places -- would make namei() walk 126 zero
// slots on every lookup and break empty-slot reuse.
//
TEST(Dir, SizeIsNotBlockRounded)
{
    const char *path = "dir_size.img";
    Filesystem fs;
    build(fs, path);

    Inode dp;
    make_dir(fs, dp, 2, 2);
    EXPECT_EQ(dp.size, 2 * DIRENTSZ) << "an empty directory is two entries, 48 bytes";
    EXPECT_NE(dp.size, BSIZE) << "it must NOT be rounded up to a block";

    dir::enter(dp, "one", 3);
    EXPECT_EQ(dp.size, 3 * DIRENTSZ);

    dir::enter(dp, "two", 4);
    EXPECT_EQ(dp.size, 4 * DIRENTSZ);

    fs.image.close();
    std::remove(path);
}

//
// DIRPB entries fill a block exactly, and entry 128 starts the next one.
//
TEST(Dir, EntriesTileTheBlock)
{
    const char *path = "dir_tile.img";
    Filesystem fs;
    build(fs, path);

    Inode dp;
    make_dir(fs, dp, 2, 2);

    // Fill the first block exactly: 128 entries, of which 2 are already there.
    for (int i = 2; i < DIRPB; i++)
        dir::enter(dp, "f" + std::to_string(i), 100 + i);

    EXPECT_EQ(dp.size, int64_t(DIRPB) * DIRENTSZ);
    EXPECT_EQ(dp.size, BSIZE) << "DIRPB entries are exactly one block";
    EXPECT_EQ(dp.addr[1], 0) << "and no second block yet";

    // One more spills into the next block.
    dir::enter(dp, "spill", 999);
    EXPECT_GT(dp.addr[1], 0);
    EXPECT_EQ(dir::lookup(dp, "spill"), 999);

    // Everything is still findable across the boundary.
    for (int i = 2; i < DIRPB; i++)
        ASSERT_EQ(dir::lookup(dp, "f" + std::to_string(i)), 100 + i) << "entry " << i;

    fs.image.close();
    std::remove(path);
}

TEST(Dir, LookupAndUnlink)
{
    const char *path = "dir_lookup.img";
    Filesystem fs;
    build(fs, path);

    Inode dp;
    make_dir(fs, dp, 2, 2);

    dir::enter(dp, "alpha", 10);
    dir::enter(dp, "beta", 11);
    dir::enter(dp, "gamma", 12);

    EXPECT_EQ(dir::lookup(dp, "."), 2);
    EXPECT_EQ(dir::lookup(dp, ".."), 2);
    EXPECT_EQ(dir::lookup(dp, "alpha"), 10);
    EXPECT_EQ(dir::lookup(dp, "beta"), 11);
    EXPECT_EQ(dir::lookup(dp, "gamma"), 12);
    EXPECT_EQ(dir::lookup(dp, "delta"), 0) << "absent names return 0";

    EXPECT_TRUE(dir::unlink(dp, "beta"));
    EXPECT_EQ(dir::lookup(dp, "beta"), 0);
    EXPECT_FALSE(dir::unlink(dp, "beta")) << "unlinking twice fails";

    // The others are untouched, and the directory did not shrink.
    EXPECT_EQ(dir::lookup(dp, "alpha"), 10);
    EXPECT_EQ(dir::lookup(dp, "gamma"), 12);
    EXPECT_EQ(dp.size, 5 * DIRENTSZ) << "v7 leaves the dead slot in place";

    fs.image.close();
    std::remove(path);
}

//
// A freed slot is reused rather than the directory growing.
//
TEST(Dir, EmptySlotIsReused)
{
    const char *path = "dir_reuse.img";
    Filesystem fs;
    build(fs, path);

    Inode dp;
    make_dir(fs, dp, 2, 2);
    dir::enter(dp, "one", 10);
    dir::enter(dp, "two", 11);

    const int64_t was = dp.size;
    ASSERT_TRUE(dir::unlink(dp, "one"));
    dir::enter(dp, "three", 12);

    EXPECT_EQ(dp.size, was) << "the freed slot should have been reused";
    EXPECT_EQ(dir::lookup(dp, "three"), 12);
    EXPECT_EQ(dir::lookup(dp, "two"), 11);

    fs.image.close();
    std::remove(path);
}

//
// THE truncation test.  v7 truncates a name to DIRSIZ silently, so two names
// sharing an 18-character prefix are the same name.  This is behaviour to be
// aware of, not a bug to fix -- but a caller building an image from a host tree
// should warn, which is what dir::name_is_truncated() is for.
//
TEST(Dir, LongNamesTruncateAndCollide)
{
    const char *path = "dir_trunc.img";
    Filesystem fs;
    build(fs, path);

    Inode dp;
    make_dir(fs, dp, 2, 2);

    const std::string a = "abcdefghijklmnopqrSTUV"; // 22 chars
    const std::string b = "abcdefghijklmnopqrWXYZ"; // same first 18

    EXPECT_TRUE(dir::name_is_truncated(a));
    EXPECT_FALSE(dir::name_is_truncated("abcdefghijklmnopqr")); // exactly 18

    dir::enter(dp, a, 30);

    // Stored truncated ...
    DirEntry e;
    ASSERT_TRUE(dir::get(dp, 2, e));
    EXPECT_STREQ(e.name, "abcdefghijklmnopqr");

    // ... and therefore the two names collide.
    EXPECT_EQ(dir::lookup(dp, b), 30) << "18 characters is the whole name";
    EXPECT_THROW(dir::enter(dp, b, 31), FsError) << "and so this is a duplicate";

    fs.image.close();
    std::remove(path);
}

//
// A short name is NUL-padded across the full DIRSIZ, because namei() compares all
// DIRSIZ characters.  Stale bytes in the tail would stop a name matching itself.
//
TEST(Dir, ShortNamesArePadded)
{
    const char *path = "dir_pad.img";
    Filesystem fs;
    build(fs, path);

    Inode dp;
    make_dir(fs, dp, 2, 2);

    // Write a long name, delete it, then reuse the slot with a short one.
    dir::enter(dp, "abcdefghijklmnopqr", 20);
    ASSERT_TRUE(dir::unlink(dp, "abcdefghijklmnopqr"));
    dir::enter(dp, "ab", 21);

    DirEntry e;
    ASSERT_TRUE(dir::get(dp, 2, e));
    EXPECT_STREQ(e.name, "ab") << "the old name's tail must not survive";
    EXPECT_EQ(dir::lookup(dp, "ab"), 21);

    // And on disk: word 0 holds "ab", words 1 and 2 are zero.
    Block blk;
    fs.image.read_block(dp.addr[0], blk);
    EXPECT_EQ(blk[2 * DIRWORDS + DE_NAME + 0], 0x616200000000ULL);
    EXPECT_EQ(blk[2 * DIRWORDS + DE_NAME + 1], 0ULL);
    EXPECT_EQ(blk[2 * DIRWORDS + DE_NAME + 2], 0ULL);

    fs.image.close();
    std::remove(path);
}

TEST(Dir, RefusesDuplicatesAndBadArguments)
{
    const char *path = "dir_bad.img";
    Filesystem fs;
    build(fs, path);

    Inode dp;
    make_dir(fs, dp, 2, 2);
    dir::enter(dp, "one", 10);

    EXPECT_THROW(dir::enter(dp, "one", 11), FsError);
    EXPECT_THROW(dir::enter(dp, ".", 11), FsError);
    EXPECT_THROW(dir::enter(dp, "", 11), FsError);
    EXPECT_THROW(dir::enter(dp, "ok", 0), FsError);

    fs.image.close();
    std::remove(path);
}

//
// A big directory: more than one block, forced through bmap's indirect path.
//
TEST(Dir, LargeDirectory)
{
    const char *path = "dir_large.img";
    Filesystem fs;
    build(fs, path);

    Inode dp;
    make_dir(fs, dp, 2, 2);

    constexpr int N = 1000; // ~8 blocks of entries
    for (int i = 0; i < N; i++)
        dir::enter(dp, "file" + std::to_string(i), 100 + i);

    EXPECT_EQ(dp.size, int64_t(N + 2) * DIRENTSZ);

    for (int i = 0; i < N; i++)
        ASSERT_EQ(dir::lookup(dp, "file" + std::to_string(i)), 100 + i) << "file" << i;

    // And each() sees every slot, empty ones included.
    int64_t seen = 0;
    dir::each(dp, [&](int64_t, const DirEntry &) { seen++; });
    EXPECT_EQ(seen, N + 2);

    fs.image.close();
    std::remove(path);
}
