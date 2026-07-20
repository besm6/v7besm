//
// fsck.
//
// A checker that finds nothing is indistinguishable from a checker that does
// nothing, so almost every test here CORRUPTS a known-good image in one specific
// way and asserts that the damage is found.  The first test establishes that a
// freshly built image is clean, which is what makes the rest meaningful.
//
#include "check.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <sstream>

#include "command.h"
#include "create.h"
#include "dir.h"
#include "inode.h"

namespace {

constexpr int64_t NOW = 1000000000;

//
// Build a small but structurally complete filesystem: nested directories, a
// multi-block file, a hard link and a device node.  Small enough to be quick,
// varied enough that each pass has something to walk.
//
void build(const char *path, const char *scratch)
{
    {
        std::ofstream f(scratch, std::ios::binary);
        const std::string block(BSIZE, 'x');
        for (int i = 0; i < 8; i++) // 8 blocks: past the 6 direct, into the indirect
            f << block;
    }

    Filesystem fs;
    create_filesystem(fs, path, 400, 0, NOW);

    cmd::make_directory(fs, "/etc", 0755, 0, 0, NOW);
    cmd::make_directory(fs, "/usr", 0755, 0, 0, NOW);
    cmd::add_file(fs, "/etc/passwd", scratch, 0644, 0, 0, NOW);
    cmd::add_file(fs, "/usr/data", scratch, 0644, 0, 0, NOW);
    cmd::add_hard_link(fs, "/etc/passwd.bak", "/etc/passwd");
    cmd::add_device(fs, "/dev/tty", false, 5, 0, 0620, 0, 0, NOW);
    fs.sync(true);
    fs.close();
}

//
// Run the checker and give back both the count and what it said.
//
int check(const char *path, std::string *text = nullptr)
{
    Filesystem fs;
    fs.open(path, false);

    Options opt;
    Checker chk(fs, opt);

    std::ostringstream out;
    const int n = chk.run(out);
    if (text)
        *text = out.str();

    fs.close();
    return n;
}

struct Scratch {
    const char *path;
    const char *scratch;
    Scratch(const char *p, const char *s) : path(p), scratch(s) { build(path, scratch); }
    ~Scratch()
    {
        std::remove(path);
        std::remove(scratch);
    }
};

} // namespace

//
// The baseline: what this tool builds, this tool considers correct.  Everything
// below depends on this being true.
//
TEST(Check, FreshImageIsClean)
{
    Scratch img("ck_clean.img", "ck_clean.dat");

    std::string text;
    EXPECT_EQ(check(img.path, &text), 0) << text;
}

//
// The superblock is checked first, and a bad one stops the run -- every later
// pass sizes its arrays from s_fsize and s_isize.
//
TEST(Check, RejectsBadSuperblock)
{
    Scratch img("ck_sb.img", "ck_sb.dat");

    {
        Filesystem fs;
        fs.open(img.path, true);
        fs.sb.magic = 0;
        fs.sb.save(fs.image);
        fs.close();
    }

    std::string text;
    EXPECT_GT(check(img.path, &text), 0);
    EXPECT_NE(text.find("not a filesystem"), std::string::npos) << text;
}

//
// Two files claiming the same block -- the most destructive thing that can be
// wrong, and the reason pass 1 exists.
//
TEST(Check, FindsDuplicateBlock)
{
    Scratch img("ck_dup.img", "ck_dup.dat");

    {
        Filesystem fs;
        fs.open(img.path, true);

        Inode a, b;
        a.get(fs, cmd::namei(fs, "/etc/passwd"));
        b.get(fs, cmd::namei(fs, "/usr/data"));

        // Point one file's first block at the other's.
        b.addr[0] = a.addr[0];
        b.dirty   = true;
        b.save();
        fs.close();
    }

    std::string text;
    EXPECT_GT(check(img.path, &text), 0);
    EXPECT_NE(text.find("already claimed"), std::string::npos) << text;
}

//
// A block address pointing outside the data area.
//
TEST(Check, FindsOutOfRangeBlock)
{
    Scratch img("ck_range.img", "ck_range.dat");

    {
        Filesystem fs;
        fs.open(img.path, true);

        Inode ip;
        ip.get(fs, cmd::namei(fs, "/usr/data"));
        ip.addr[0] = 99999; // past the end of the volume
        ip.dirty   = true;
        ip.save();
        fs.close();
    }

    std::string text;
    EXPECT_GT(check(img.path, &text), 0);
    EXPECT_NE(text.find("outside the data area"), std::string::npos) << text;
}

//
// A wrong link count: the file is referenced twice but claims one link.
//
TEST(Check, FindsWrongLinkCount)
{
    Scratch img("ck_nlink.img", "ck_nlink.dat");

    {
        Filesystem fs;
        fs.open(img.path, true);

        Inode ip;
        ip.get(fs, cmd::namei(fs, "/etc/passwd")); // has 2 links
        ip.nlink = 1;
        ip.dirty = true;
        ip.save();
        fs.close();
    }

    std::string text;
    EXPECT_GT(check(img.path, &text), 0);
    EXPECT_NE(text.find("link count"), std::string::npos) << text;
}

//
// An allocated inode nothing points at.
//
TEST(Check, FindsOrphanInode)
{
    Scratch img("ck_orphan.img", "ck_orphan.dat");

    {
        Filesystem fs;
        fs.open(img.path, true);

        // Allocate an inode and never link it into the tree.
        const int64_t ino = fs.inode_alloc();
        Inode ip;
        ip.get(fs, ino);
        ip.clear();
        ip.mode  = IFREG | 0644;
        ip.nlink = 1;
        ip.save(true);
        fs.sync(true);
        fs.close();
    }

    std::string text;
    EXPECT_GT(check(img.path, &text), 0);
    EXPECT_NE(text.find("unreferenced"), std::string::npos) << text;
}

//
// A directory entry pointing at an inode that is not allocated.
//
TEST(Check, FindsDanglingDirectoryEntry)
{
    Scratch img("ck_dangle.img", "ck_dangle.dat");

    {
        Filesystem fs;
        fs.open(img.path, true);

        Inode etc;
        etc.get(fs, cmd::namei(fs, "/etc"));
        dir::enter(etc, "ghost", 200); // inside the i-list (224 inodes) but never allocated
        etc.save(true);
        fs.sync(true);
        fs.close();
    }

    std::string text;
    EXPECT_GT(check(img.path, &text), 0);
    EXPECT_NE(text.find("not allocated"), std::string::npos) << text;
}

//
// An i-number outside the i-list altogether.
//
TEST(Check, FindsOutOfRangeInumber)
{
    Scratch img("ck_inorange.img", "ck_inorange.dat");

    {
        Filesystem fs;
        fs.open(img.path, true);

        Inode etc;
        etc.get(fs, cmd::namei(fs, "/etc"));
        dir::enter(etc, "wild", 999999);
        etc.save(true);
        fs.sync(true);
        fs.close();
    }

    std::string text;
    EXPECT_GT(check(img.path, &text), 0);
    EXPECT_NE(text.find("outside the i-list"), std::string::npos) << text;
}

//
// A `..' that points somewhere other than the parent.
//
TEST(Check, FindsWrongDotDot)
{
    Scratch img("ck_dotdot.img", "ck_dotdot.dat");

    {
        Filesystem fs;
        fs.open(img.path, true);

        Inode etc;
        etc.get(fs, cmd::namei(fs, "/etc"));
        ASSERT_TRUE(dir::unlink(etc, ".."));
        dir::enter(etc, "..", cmd::namei(fs, "/usr")); // should be the root
        etc.save(true);
        fs.sync(true);
        fs.close();
    }

    std::string text;
    EXPECT_GT(check(img.path, &text), 0);
    EXPECT_NE(text.find("`..' points at"), std::string::npos) << text;
}

//
// A block that is both on the free list and in a file.
//
TEST(Check, FindsBlockBothUsedAndFree)
{
    Scratch img("ck_bothfree.img", "ck_bothfree.dat");

    {
        Filesystem fs;
        fs.open(img.path, true);

        Inode ip;
        ip.get(fs, cmd::namei(fs, "/usr/data"));

        // Put a block the file owns back on the free list without releasing it.
        fs.block_free(ip.addr[0]);
        fs.sync(true);
        fs.close();
    }

    std::string text;
    EXPECT_GT(check(img.path, &text), 0);
    EXPECT_NE(text.find("both in use and on the free list"), std::string::npos) << text;
}

//
// A chain block with a nonsense count.
//
TEST(Check, FindsBadChainCount)
{
    Scratch img("ck_chain.img", "ck_chain.dat");

    {
        Filesystem fs;
        fs.open(img.path, true);

        //
        // Drain the superblock cache so the next allocation would reload from a
        // chain block, then corrupt that block's count.
        //
        const int64_t chain = fs.sb.free[0];
        if (chain != 0) {
            Block b;
            fs.image.read_block(chain, b);
            b[FB_NFREE] = to_word(NICFREE + 99);
            fs.image.write_block(chain, b);
        }
        fs.close();
    }

    std::string text;
    const int n = check(img.path, &text);
    // Either the bad count or the lost blocks behind it must be reported.
    EXPECT_GT(n, 0) << text;
}

//
// Blocks that belong to nothing: not in a file, not on the free list.
//
TEST(Check, FindsLostBlocks)
{
    Scratch img("ck_lost.img", "ck_lost.dat");

    {
        Filesystem fs;
        fs.open(img.path, true);

        // Take blocks out of the free list and give them to nobody.
        for (int i = 0; i < 5; i++)
            fs.block_alloc();
        fs.sync(true);
        fs.close();
    }

    std::string text;
    EXPECT_GT(check(img.path, &text), 0);
    EXPECT_NE(text.find("lost space"), std::string::npos) << text;
}

//
// A directory whose size is not a whole number of entries -- which would make
// namei() read a partial entry off the end.
//
TEST(Check, FindsRaggedDirectorySize)
{
    Scratch img("ck_ragged.img", "ck_ragged.dat");

    {
        Filesystem fs;
        fs.open(img.path, true);

        Inode etc;
        etc.get(fs, cmd::namei(fs, "/etc"));
        etc.size += 3; // no longer a multiple of DIRENTSZ
        etc.dirty = true;
        etc.save();
        fs.close();
    }

    std::string text;
    EXPECT_GT(check(img.path, &text), 0);
    EXPECT_NE(text.find("not a multiple of"), std::string::npos) << text;
}

//
// A device node's di_addr[0] is its major/minor, NOT a block address.  Walking it
// as one would claim a nonsense block and report a spurious error -- so this test
// exists to prove the checker does not.
//
TEST(Check, DoesNotWalkDeviceAddressesAsBlocks)
{
    Scratch img("ck_dev.img", "ck_dev.dat");

    std::string text;
    EXPECT_EQ(check(img.path, &text), 0)
        << "a device node must not be treated as owning block " << makedev(5, 0) << ":\n"
        << text;
}

//
// The checker can run twice in one process and give the same answer -- the state
// is per-object, not file-scope as it was in the original.
//
TEST(Check, IsRepeatable)
{
    Scratch img("ck_repeat.img", "ck_repeat.dat");

    Filesystem fs;
    fs.open(img.path, false);
    Options opt;

    std::ostringstream a, b;
    Checker one(fs, opt);
    Checker two(fs, opt);

    EXPECT_EQ(one.run(a), 0) << a.str();
    EXPECT_EQ(two.run(b), 0) << b.str();
    EXPECT_EQ(a.str(), b.str());

    fs.close();
}
