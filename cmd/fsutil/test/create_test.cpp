//
// Making a filesystem, end to end.
//
// The numbers asserted here -- isize 34, tfree 1965, the root at block 34 -- are
// not arbitrary: they are what a 2000-block EC-5052 volume comes to under the
// sizing rule in create.cpp, and pinning them means a change to that rule has to
// be deliberate.
//
#include "create.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <sstream>

#include "dir.h"
#include "inode.h"

namespace {

constexpr int64_t NOW = 1000000000;

} // namespace

//
// The whole superblock of a fresh 2000-block volume.
//
TEST(Create, SuperblockOfAFreshVolume)
{
    const char *path = "cr_super.img";
    Filesystem fs;
    create_filesystem(fs, path, MDNBLK, 0, NOW);

    EXPECT_EQ(fs.sb.magic, int64_t(FS_MAGIC));
    EXPECT_EQ(fs.sb.bsize, BSIZEW);
    EXPECT_EQ(fs.sb.inopb, INOPB);
    EXPECT_EQ(fs.sb.naddr, NADDR);
    EXPECT_EQ(fs.sb.fsize, 2000);
    EXPECT_EQ(fs.sb.time, NOW);

    //
    // 1000 inodes wanted -> 32 blocks of 32 -> 1024 inodes, first data block at
    // 2 + 32 == 34.
    //
    EXPECT_EQ(fs.sb.isize, 34);

    // 2000 - 34 == 1966 data blocks, less the one the root directory took.
    EXPECT_EQ(fs.sb.tfree, 1965);

    // 1024 inodes, less inode 1 (never allocatable) and the root.
    EXPECT_EQ(fs.sb.tinode, 1022);

    EXPECT_EQ(fs.sb.ninode, NICINOD);
    EXPECT_EQ(fs.sb.flock, 0);
    EXPECT_EQ(fs.sb.ilock, 0);
    EXPECT_EQ(fs.sb.fmod, 0);
    EXPECT_EQ(fs.sb.ronly, 0);

    fs.close();
    std::remove(path);
}

//
// It must pass sbcheck() -- the kernel's own test, transcribed in
// SuperBlock::validate().  A fresh image that fails this will not mount.
//
TEST(Create, PassesSbcheck)
{
    const char *path = "cr_valid.img";
    Filesystem fs;
    create_filesystem(fs, path, MDNBLK, 0, NOW);

    std::ostringstream err;
    EXPECT_TRUE(fs.sb.validate(err)) << err.str();

    // And still passes after being written out and read back.
    fs.close();

    Filesystem again;
    again.open(path, false);
    std::ostringstream err2;
    EXPECT_TRUE(again.sb.validate(err2)) << err2.str();
    again.close();

    std::remove(path);
}

//
// The root directory: inode 2, two links, 48 bytes, one block, `.' and `..' both
// pointing at itself.
//
TEST(Create, RootDirectory)
{
    const char *path = "cr_root.img";
    Filesystem fs;
    create_filesystem(fs, path, MDNBLK, 0, NOW);

    Inode root;
    root.get(fs, ROOTINO);

    EXPECT_EQ(root.mode, IFDIR | 0777);
    EXPECT_EQ(root.nlink, 2);
    EXPECT_EQ(root.uid, 0);
    EXPECT_EQ(root.gid, 0);
    EXPECT_EQ(root.mtime, NOW);
    EXPECT_TRUE(root.is_dir());

    // 48 BYTES -- two entries -- not a rounded-up block.
    EXPECT_EQ(root.size, 2 * DIRENTSZ);

    // The first data block, since the free list hands them out ascending.
    EXPECT_EQ(root.addr[0], 34);
    for (int i = 1; i < NADDR; i++)
        EXPECT_EQ(root.addr[i], 0) << "di_addr[" << i << "]";

    EXPECT_EQ(dir::lookup(root, "."), ROOTINO);
    EXPECT_EQ(dir::lookup(root, ".."), ROOTINO);

    // And nothing else is in there: slots 2..127 are all zero.
    Block b;
    fs.image.read_block(root.addr[0], b);
    for (int i = 2 * DIRWORDS; i < BSIZEW; i++)
        ASSERT_EQ(b[i], 0ULL) << "root block word " << i << " must be zero";

    fs.close();
    std::remove(path);
}

//
// Inode 1 is left unallocated on purpose: ialloc() will not hand out anything
// below ROOTINO, so v7's dummy inode there buys nothing.
//
TEST(Create, InodeOneIsUnused)
{
    const char *path = "cr_ino1.img";
    Filesystem fs;
    create_filesystem(fs, path, MDNBLK, 0, NOW);

    Inode one;
    one.get(fs, 1);
    EXPECT_EQ(one.mode, 0);
    EXPECT_EQ(one.nlink, 0);
    EXPECT_FALSE(one.is_allocated());

    fs.close();
    std::remove(path);
}

//
// The i-list is zeroed, so ialloc()'s "free means di_mode == 0" test works.
//
TEST(Create, IlistIsClean)
{
    const char *path = "cr_ilist.img";
    Filesystem fs;
    create_filesystem(fs, path, MDNBLK, 0, NOW);

    for (int64_t ino = 3; ino <= 1024; ino++) {
        Inode ip;
        ip.get(fs, ino);
        ASSERT_EQ(ip.mode, 0) << "inode " << ino << " is not clean";
        ASSERT_EQ(ip.size, 0) << "inode " << ino;
    }

    fs.close();
    std::remove(path);
}

//
// The free-inode cache is seeded descending, so ialloc() -- which pops from the
// top -- hands out the lowest number first.
//
TEST(Create, InodeCacheHandsOutLowestFirst)
{
    const char *path = "cr_icache.img";
    Filesystem fs;
    create_filesystem(fs, path, MDNBLK, 0, NOW);

    ASSERT_EQ(fs.sb.ninode, NICINOD);

    // The top of the stack is the next one out.
    EXPECT_EQ(fs.sb.inode[size_t(fs.sb.ninode - 1)], ROOTINO + 1)
        << "the first inode handed out should be 3";
    EXPECT_EQ(fs.sb.inode[0], ROOTINO + NICINOD) << "and the last of the cached run should be 162";

    fs.close();
    std::remove(path);
}

//
// The free list is in the state alloc() expects: ascending allocation, starting
// at the first data block.
//
TEST(Create, AllocationStartsAtFirstDataBlock)
{
    const char *path = "cr_alloc.img";
    Filesystem fs;
    create_filesystem(fs, path, MDNBLK, 0, NOW);

    // Block 34 went to the root, so the next one out is 35.
    for (int64_t expect = 35; expect < 35 + 100; expect++)
        ASSERT_EQ(fs.block_alloc(), expect);

    fs.close();
    std::remove(path);
}

//
// Sizes other than the default drive.
//
TEST(Create, OtherVolumeSizes)
{
    const struct {
        int64_t nblk;
        int64_t ninodes;
        int64_t isize;
    } cases[] = {
        { 2000, 0, 34 },     // the default: 1000 wanted -> 32 blocks
        { 512, 0, 10 },      // 256 wanted -> 8 blocks
        { 100, 0, 4 },       // 50 wanted -> 2 blocks
        { 2000, 64, 4 },     // explicit: 64 inodes -> 2 blocks
        { 2000, 4000, 127 }, // explicit: 4000 -> 125 blocks
    };

    for (const auto &c : cases) {
        const char *path = "cr_sizes.img";
        Filesystem fs;
        create_filesystem(fs, path, c.nblk, c.ninodes, NOW);

        EXPECT_EQ(fs.sb.isize, c.isize) << "nblk " << c.nblk << " ninodes " << c.ninodes;
        EXPECT_EQ(fs.sb.fsize, c.nblk);

        std::ostringstream err;
        EXPECT_TRUE(fs.sb.validate(err)) << err.str();

        Inode root;
        root.get(fs, ROOTINO);
        EXPECT_EQ(dir::lookup(root, "."), ROOTINO);

        fs.close();
        std::remove(path);
    }
}

//
// Sizes that cannot work are refused, rather than producing an image that fails
// later.
//
TEST(Create, RefusesImpossibleSizes)
{
    const char *path = "cr_bad.img";
    Filesystem fs;

    EXPECT_THROW(create_filesystem(fs, path, 4, 0, NOW), FsError);          // too small
    EXPECT_THROW(create_filesystem(fs, path, MDNBLK + 1, 0, NOW), FsError); // past the drive
    EXPECT_THROW(create_filesystem(fs, path, 100, 100000, NOW), FsError);   // i-list eats it all

    std::remove(path);
}

//
// Building a small tree in a fresh filesystem, then reading it back -- the first
// end-to-end exercise of create, alloc, inode and dir together.
//
TEST(Create, PopulateAndReadBack)
{
    const char *path = "cr_tree.img";
    Filesystem fs;
    create_filesystem(fs, path, MDNBLK, 0, NOW);

    Inode root;
    root.get(fs, ROOTINO);

    // A subdirectory ...
    Inode etc;
    etc.get(fs, 3);
    etc.clear();
    etc.mode  = IFDIR | 0755;
    etc.nlink = 2;
    dir::make_empty(etc, ROOTINO);
    dir::enter(root, "etc", 3);
    root.nlink++; // etc/.. points back here
    etc.save(true);

    // ... with a file in it.
    Inode passwd;
    passwd.get(fs, 4);
    passwd.clear();
    passwd.mode  = IFREG | 0644;
    passwd.nlink = 1;

    const std::string text = "root::0:0:Charlie Root:/:/bin/sh\n";
    passwd.write(0, reinterpret_cast<const uint8_t *>(text.data()), int64_t(text.size()));
    passwd.save(true);
    dir::enter(etc, "passwd", 4);
    etc.save(true);
    root.save(true);

    fs.close();

    // Reopen and walk it as the kernel would.
    Filesystem again;
    again.open(path, false);

    Inode r;
    r.get(again, ROOTINO);
    const int64_t etc_ino = dir::lookup(r, "etc");
    ASSERT_EQ(etc_ino, 3);

    Inode e;
    e.get(again, etc_ino);
    ASSERT_TRUE(e.is_dir());
    EXPECT_EQ(dir::lookup(e, ".."), ROOTINO);

    const int64_t pw_ino = dir::lookup(e, "passwd");
    ASSERT_EQ(pw_ino, 4);

    Inode p;
    p.get(again, pw_ino);
    EXPECT_TRUE(p.is_reg());
    EXPECT_EQ(p.size, int64_t(text.size()));

    std::string back(size_t(p.size), '\0');
    ASSERT_EQ(p.read(0, reinterpret_cast<uint8_t *>(&back[0]), p.size), p.size);
    EXPECT_EQ(back, text);

    again.close();
    std::remove(path);
}
