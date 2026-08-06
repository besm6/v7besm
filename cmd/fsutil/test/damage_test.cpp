//
// The `-D' verb: deliberate corruption.
//
// Two things are asserted of every case, and the second is the one that matters.
// The obvious one is that the word really changed.  The other is that the CHECKER
// then finds it -- because the whole reason this verb exists is to feed cmd/fsck
// and cmd/fsutil's own Checker the same broken filesystem and require them to
// agree, and a damage spec that produces a still-valid image would make a test
// pass by asserting nothing.
//
#include "damage.h"

#include <gtest/gtest.h>
#include <unistd.h>

#include <cstdio>
#include <sstream>
#include <string>

#include "check.h"
#include "command.h"
#include "create.h"
#include "dir.h"
#include "inode.h"

namespace {

constexpr int64_t NOW = 1000000000;

//
// The image name carries the process id, because ctest runs every case as its own
// process and all of them share this directory: under a fixed name two concurrent
// cases would build and damage the same file.  Within one process the cases run in
// turn, and SetUp/TearDown rebuild and remove it around each.
//
const std::string IMG = "damage_test." + std::to_string(getpid()) + ".img";

//
// A small filesystem with a directory, a file and a hard link -- enough structure
// for an entry to dangle and a link count to be wrong.
//
void build()
{
    Filesystem fs;
    create_filesystem(fs, IMG, 200, 0, NOW);

    cmd::make_directory(fs, "/etc", 0755, 0, 0, NOW);
    cmd::add_device(fs, "/dev/tty", false, 5, 0, 0620, 0, 0, NOW);
    cmd::add_hard_link(fs, "/dev/tty2", "/dev/tty");
    fs.sync(true);
    fs.close();
}

//
// Apply one spec, discarding the log.  Opens and closes around it, as the tool
// does, so that a spec which fails to reach the disk cannot pass by leaving the
// value in an in-core mirror.
//
void damage_one(const std::string &spec)
{
    Filesystem fs;
    fs.open(IMG, true);

    std::ostringstream log;
    damage::apply(fs, spec, log);

    fs.close();
}

Word word_at(int64_t bno, int w)
{
    Filesystem fs;
    fs.open(IMG, false);

    Block b;
    fs.image.read_block(bno, b);

    fs.close();
    return b[size_t(w)];
}

int problems()
{
    Filesystem fs;
    fs.open(IMG, false);

    Options opt;
    Checker chk(fs, opt);

    std::ostringstream out;
    const int n = chk.run(out);

    fs.close();
    return n;
}

std::string expect_throw(const std::string &spec)
{
    Filesystem fs;
    fs.open(IMG, true);

    std::ostringstream log;
    std::string what;
    try {
        damage::apply(fs, spec, log);
    } catch (const FsError &e) {
        what = e.what();
    }
    fs.close();
    return what;
}

class Damage : public ::testing::Test {
protected:
    void SetUp() override { build(); }
    void TearDown() override { std::remove(IMG.c_str()); }
};

//
// The premise: what build() made is clean, so every count below is the damage and
// nothing else.
//
TEST_F(Damage, TheFixtureIsClean)
{
    EXPECT_EQ(problems(), 0);
}

TEST_F(Damage, SuperblockField)
{
    damage_one("sb.nfree=999");
    EXPECT_EQ(from_word(word_at(SUPERB, SB_NFREE)), 999);
    EXPECT_GT(problems(), 0);
}

//
// The superblock is the one target sync() would undo, which is why apply() does
// not call it.  If it ever starts to, this is the test that fails.
//
TEST_F(Damage, SuperblockDamageSurvivesTheClose)
{
    damage_one("sb.magic=0");
    EXPECT_EQ(from_word(word_at(SUPERB, SB_MAGIC)), 0);
}

TEST_F(Damage, SuperblockCachedArray)
{
    damage_one("sb.free3=1");
    EXPECT_EQ(from_word(word_at(SUPERB, SB_FREE + 3)), 1);
}

TEST_F(Damage, InodeField)
{
    damage_one("i2.nlink=7"); // the root
    EXPECT_EQ(from_word(word_at(itod(ROOTINO), itoo(ROOTINO) * DI_WORDS + DI_NLINK)), 7);
    EXPECT_GT(problems(), 0);
}

//
// A mode is written in octal, because that is how the world writes one.
//
TEST_F(Damage, InodeModeIsOctal)
{
    damage_one("i2.mode=0100644");
    EXPECT_EQ(from_word(word_at(itod(ROOTINO), itoo(ROOTINO) * DI_WORDS + DI_MODE)), 0100644);
}

TEST_F(Damage, InodeAddress)
{
    damage_one("i2.addr0=1"); // inside the i-list: not a data block
    EXPECT_EQ(from_word(word_at(itod(ROOTINO), itoo(ROOTINO) * DI_WORDS + DI_ADDR)), 1);
    EXPECT_GT(problems(), 0);
}

//
// A directory entry, reached through bmap() rather than through arithmetic in the
// caller.  Entry 2 of the root is the first name after `.' and `..'.
//
TEST_F(Damage, DirectoryEntry)
{
    damage_one("e2.2=9999");

    Filesystem fs;
    fs.open(IMG, false);
    Inode dp;
    dp.get(fs, ROOTINO);
    DirEntry e;
    ASSERT_TRUE(dir::get(dp, 2, e));
    EXPECT_EQ(e.ino, 9999);
    fs.close();

    EXPECT_GT(problems(), 0);
}

TEST_F(Damage, RawBlockWord)
{
    damage_one("b1.0=0"); // the superblock's magic, the long way round
    EXPECT_EQ(from_word(word_at(1, 0)), 0);
}

//
// A value outside 41 signed bits.  to_word() would throw; this must not -- writing
// a word the format cannot hold is exactly what a corruption tool is for.
//
TEST_F(Damage, WritesAValueThatDoesNotFitAnInt)
{
    damage_one("b1.20=0777777777777777777");
    EXPECT_EQ(word_at(1, 20), Word(0777777777777777777ULL) & WORD_MASK);
}

TEST_F(Damage, AppliesSeveralInOrder)
{
    damage_one("sb.nfree=1");
    damage_one("sb.nfree=2");
    EXPECT_EQ(from_word(word_at(SUPERB, SB_NFREE)), 2);
}

TEST_F(Damage, SaysWhatItDid)
{
    Filesystem fs;
    fs.open(IMG, true);

    std::ostringstream log;
    damage::apply(fs, "sb.nfree=999", log);
    fs.close();

    EXPECT_NE(log.str().find("sb.nfree"), std::string::npos);
    EXPECT_NE(log.str().find("999"), std::string::npos);
}

//
// The diagnostics.  Each of these is a typo somebody will make.
//
TEST_F(Damage, RefusesNonsense)
{
    EXPECT_NE(expect_throw("sb.nfree").find("expected <target>=<value>"), std::string::npos);
    EXPECT_NE(expect_throw("sbnfree=1").find("expected sb."), std::string::npos);
    EXPECT_NE(expect_throw("sb.nofield=1").find("no superblock field"), std::string::npos);
    EXPECT_NE(expect_throw("sb.nfree=12x").find("is not a number"), std::string::npos);
    EXPECT_NE(expect_throw("i2.nofield=1").find("no inode field"), std::string::npos);
    EXPECT_NE(expect_throw("i0.mode=1").find("outside the i-list"), std::string::npos);
    EXPECT_NE(expect_throw("i99999.mode=1").find("outside the i-list"), std::string::npos);
    EXPECT_NE(expect_throw("i2.addr9=1").find("out of range"), std::string::npos);
    EXPECT_NE(expect_throw("b99999.0=1").find("outside the image"), std::string::npos);
    EXPECT_NE(expect_throw("b1.512=1").find("outside a block"), std::string::npos);
    EXPECT_NE(expect_throw("z1.0=1").find("expected sb"), std::string::npos);
}

//
// e<N> against something that is not a directory, and against an entry that is
// past the end of one.
//
TEST_F(Damage, RefusesABadDirectoryTarget)
{
    Filesystem fs;
    fs.open(IMG, false);
    const int64_t dev = cmd::namei(fs, "/dev/tty");
    fs.close();
    ASSERT_GT(dev, 0);

    EXPECT_NE(expect_throw("e" + std::to_string(dev) + ".0=1").find("is not a directory"),
              std::string::npos);
    EXPECT_NE(expect_throw("e2.99999=1").find("has no entry"), std::string::npos);
}

} // namespace
