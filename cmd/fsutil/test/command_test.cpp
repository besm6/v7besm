//
// The manifest parser and the verbs that turn it into a filesystem.
//
#include "command.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "create.h"
#include "dir.h"

namespace fs_std = std::filesystem;

namespace {

constexpr int64_t NOW = 1000000000;

void write_file(const std::string &path, const std::string &text)
{
    std::ofstream f(path, std::ios::binary);
    f << text;
}

//
// A scratch directory, removed on the way in so a failed earlier run cannot leak
// into this one.
//
struct Scratch {
    std::string dir;
    explicit Scratch(const char *name) : dir(name)
    {
        fs_std::remove_all(dir);
        fs_std::create_directories(dir);
    }
    ~Scratch() { fs_std::remove_all(dir); }
    std::string operator/(const std::string &leaf) const { return dir + "/" + leaf; }
};

} // namespace

TEST(Manifest, ParsesTheExampleFormat)
{
    Scratch s("mf_parse");
    const std::string mf = s / "manifest.txt";

    write_file(mf,
               "#\n"
               "# a comment\n"
               "#\n"
               "default\n"
               "owner 7\n"
               "group 8\n"
               "dirmode 0775\n"
               "filemode 0664\n"
               "\n"
               "dir /tmp\n"
               "\n"
               "file /etc/passwd\n"
               "mode 0444\n"
               "\n"
               "link /etc/aliases\n"
               "target mail/aliases\n"
               "\n"
               "cdev /dev/tty\n"
               "major 5\n"
               "minor 0\n"
               "\n"
               "bdev /dev/sda\n"
               "major 8\n"
               "minor 1\n");

    Manifest m;
    m.load(mf);

    ASSERT_EQ(m.entries().size(), 5u);
    EXPECT_EQ(m.owner, 7);
    EXPECT_EQ(m.group, 8);

    EXPECT_EQ(m.entries()[0].type, 'd');
    EXPECT_EQ(m.entries()[0].path, "/tmp");
    EXPECT_EQ(m.entries()[0].mode, 0775) << "a directory takes dirmode by default";
    EXPECT_EQ(m.entries()[0].owner, 7) << "and the default owner";

    EXPECT_EQ(m.entries()[1].type, 'f');
    EXPECT_EQ(m.entries()[1].mode, 0444) << "an explicit mode wins";

    EXPECT_EQ(m.entries()[2].type, 'l');
    EXPECT_EQ(m.entries()[2].target, "mail/aliases");

    EXPECT_EQ(m.entries()[3].type, 'c');
    EXPECT_EQ(m.entries()[3].major, 5);
    EXPECT_EQ(m.entries()[3].minor, 0);

    EXPECT_EQ(m.entries()[4].type, 'b');
    EXPECT_EQ(m.entries()[4].major, 8);
    EXPECT_EQ(m.entries()[4].minor, 1);
}

//
// `symlink' is REFUSED, not ignored.  This v7 has no S_IFLNK, and silently
// dropping the entry would produce an image missing something the manifest asked
// for.
//
TEST(Manifest, RejectsSymlink)
{
    Scratch s("mf_sym");
    const std::string mf = s / "manifest.txt";
    write_file(mf, "symlink /var/tmp\ntarget /tmp\n");

    Manifest m;
    EXPECT_THROW(m.load(mf), FsError);
}

TEST(Manifest, ReportsSyntaxErrorsWithLineNumbers)
{
    Scratch s("mf_bad");
    const std::string mf = s / "manifest.txt";
    write_file(mf, "dir /tmp\nwibble 3\n");

    try {
        Manifest m;
        m.load(mf);
        FAIL() << "expected a syntax error";
    } catch (const FsError &e) {
        const std::string msg = e.what();
        EXPECT_NE(msg.find(":2:"), std::string::npos) << "should name the line: " << msg;
        EXPECT_NE(msg.find("wibble"), std::string::npos) << msg;
    }
}

//
// Scanning a host tree and reloading the manifest it produces gives the same set
// of objects.
//
TEST(Manifest, ScanAndPrintRoundTrip)
{
    Scratch s("mf_scan");
    fs_std::create_directories(s / "etc");
    fs_std::create_directories(s / "bin");
    write_file(s / "etc/passwd", "root::0:0::/:/bin/sh\n");
    write_file(s / "bin/sh", "binary");

    Manifest m;
    m.scan(s.dir);

    // 2 directories + 2 files.
    EXPECT_EQ(m.entries().size(), 4u);

    std::ostringstream printed;
    m.print(printed);

    const std::string mf = s / "out.txt";
    write_file(mf, printed.str());

    Manifest again;
    again.load(mf);
    EXPECT_EQ(again.entries().size(), m.entries().size());
}

//
// The scan is deterministic: a manifest is a build input, and two runs over the
// same tree must produce the same file.  std::filesystem's iteration order is not
// specified, so manifest.cpp sorts.
//
TEST(Manifest, ScanIsDeterministic)
{
    Scratch s("mf_det");
    for (const char *d : { "a", "b", "c", "d", "e" }) {
        fs_std::create_directories(s / d);
        write_file(std::string(s / d) + "/file", "x");
    }

    Manifest one, two;
    one.scan(s.dir);
    two.scan(s.dir);

    ASSERT_EQ(one.entries().size(), two.entries().size());
    for (size_t i = 0; i < one.entries().size(); i++)
        EXPECT_EQ(one.entries()[i].path, two.entries()[i].path) << "entry " << i;
}

//
// Path resolution.
//
TEST(Command, Namei)
{
    const char *img = "cmd_namei.img";
    Filesystem fs;
    create_filesystem(fs, img, MDNBLK, 0, NOW);

    cmd::make_directory(fs, "/etc", 0755, 0, 0, NOW);
    cmd::make_directory(fs, "/etc/rc.d", 0755, 0, 0, NOW);

    EXPECT_EQ(cmd::namei(fs, "/"), ROOTINO);
    EXPECT_EQ(cmd::namei(fs, ""), ROOTINO);
    EXPECT_GT(cmd::namei(fs, "/etc"), 0);
    EXPECT_GT(cmd::namei(fs, "/etc/rc.d"), 0);

    // Redundant slashes make no difference.
    EXPECT_EQ(cmd::namei(fs, "//etc///rc.d"), cmd::namei(fs, "/etc/rc.d"));

    EXPECT_EQ(cmd::namei(fs, "/nope"), 0);
    EXPECT_EQ(cmd::namei(fs, "/etc/nope"), 0);

    fs.close();
    std::remove(img);
}

//
// A directory's link count: two for itself (`.' and its parent's entry), plus one
// for every subdirectory's `..'.
//
TEST(Command, DirectoryLinkCounts)
{
    const char *img = "cmd_links.img";
    Filesystem fs;
    create_filesystem(fs, img, MDNBLK, 0, NOW);

    Inode root;
    root.get(fs, ROOTINO);
    EXPECT_EQ(root.nlink, 2) << "a fresh root has `.' and its own `..'";

    cmd::make_directory(fs, "/a", 0755, 0, 0, NOW);
    cmd::make_directory(fs, "/b", 0755, 0, 0, NOW);
    cmd::make_directory(fs, "/a/c", 0755, 0, 0, NOW);

    root.get(fs, ROOTINO);
    EXPECT_EQ(root.nlink, 4) << "two subdirectories added two `..' links";

    Inode a;
    a.get(fs, cmd::namei(fs, "/a"));
    EXPECT_EQ(a.nlink, 3) << "`.', the entry in /, and c's `..'";

    Inode c;
    c.get(fs, cmd::namei(fs, "/a/c"));
    EXPECT_EQ(c.nlink, 2);
    EXPECT_EQ(dir::lookup(c, ".."), a.number);

    fs.close();
    std::remove(img);
}

//
// A hard link is a second name for one inode: same i-number, nlink 2.
//
TEST(Command, HardLink)
{
    Scratch s("cmd_hl");
    const char *img = "cmd_hl.img";
    write_file(s / "data", "shared contents\n");

    Filesystem fs;
    create_filesystem(fs, img, MDNBLK, 0, NOW);

    const int64_t ino = cmd::add_file(fs, "/one", s / "data", 0644, 0, 0, NOW);
    cmd::add_hard_link(fs, "/two", "/one");

    EXPECT_EQ(cmd::namei(fs, "/two"), ino) << "both names are the same inode";

    Inode ip;
    ip.get(fs, ino);
    EXPECT_EQ(ip.nlink, 2);

    // A directory cannot be hard-linked.
    cmd::make_directory(fs, "/d", 0755, 0, 0, NOW);
    EXPECT_THROW(cmd::add_hard_link(fs, "/dd", "/d"), FsError);

    // Nor can a link point at nothing.
    EXPECT_THROW(cmd::add_hard_link(fs, "/x", "/nowhere"), FsError);

    fs.close();
    std::remove(img);
}

//
// A device node keeps its major/minor in di_addr[0] -- NOT addr[1], which is
// where the BSD source this was ported from puts it.
//
TEST(Command, DeviceNodeUsesAddrZero)
{
    const char *img = "cmd_dev.img";
    Filesystem fs;
    create_filesystem(fs, img, MDNBLK, 0, NOW);

    const int64_t ino = cmd::add_device(fs, "/tty", false, 5, 3, 0620, 0, 0, NOW);

    Inode ip;
    ip.get(fs, ino);
    EXPECT_EQ(ip.mode & IFMT, IFCHR);
    EXPECT_EQ(major_of(ip.rdev()), 5);
    EXPECT_EQ(minor_of(ip.rdev()), 3);
    EXPECT_EQ(ip.addr[0], makedev(5, 3)) << "di_rdev IS di_addr[0]";
    EXPECT_EQ(ip.addr[1], 0) << "and addr[1] is untouched";

    EXPECT_THROW(cmd::add_device(fs, "/bad", false, 999, 0, 0, 0, 0, NOW), FsError);

    fs.close();
    std::remove(img);
}

//
// The whole pipeline: manifest -> image -> extract -> byte-compare.  This is the
// end-to-end case, and the file sizes are chosen to reach every level of
// indirection: 700 Kb is 228 blocks (past the 6 direct), 2 Mb is 651 blocks (past
// the 518-block single indirect, so into the double).
//
TEST(Command, ManifestToImageToExtract)
{
    Scratch src("cmd_e2e_src");
    Scratch dst("cmd_e2e_dst");
    const char *img = "cmd_e2e.img";

    fs_std::create_directories(src / "etc");
    fs_std::create_directories(src / "usr/lib");

    write_file(src / "etc/passwd", "root::0:0:Charlie Root:/:/bin/sh\n");

    std::string big(700000, '\0');
    for (size_t i = 0; i < big.size(); i++)
        big[i] = char(i * 31 + 7);
    write_file(src / "usr/lib/big", big);

    std::string huge(2000000, '\0');
    for (size_t i = 0; i < huge.size(); i++)
        huge[i] = char(i * 17 + 3);
    write_file(src / "usr/lib/huge", huge);

    const std::string mf = src / "manifest.txt";
    write_file(mf,
               "default\nowner 0\ngroup 0\ndirmode 0755\nfilemode 0644\n"
               "\ndir /etc\ndir /usr\ndir /usr/lib\n"
               "\nfile /etc/passwd\nsource " +
                   std::string(src / "etc/passwd") +
                   "\nmode 0444\n"
                   "\nfile /usr/lib/big\nsource " +
                   std::string(src / "usr/lib/big") +
                   "\n"
                   "\nfile /usr/lib/huge\nsource " +
                   std::string(src / "usr/lib/huge") +
                   "\n"
                   "\nlink /etc/passwd.bak\ntarget /etc/passwd\n"
                   "\ncdev /dev/tty\nmajor 5\nminor 0\n");

    Manifest m;
    m.load(mf);

    Filesystem fs;
    create_filesystem(fs, img, MDNBLK, 0, NOW);

    Options opt;
    cmd::apply(fs, m, opt, NOW);

    // The double indirect really was reached.
    Inode h;
    h.get(fs, cmd::namei(fs, "/usr/lib/huge"));
    EXPECT_EQ(h.size, 2000000);
    EXPECT_GT(h.addr[NADDR - 1], 0) << "a 651-block file must use the double indirect";

    // /dev was created implicitly by the cdev entry.
    EXPECT_GT(cmd::namei(fs, "/dev"), 0);
    EXPECT_GT(cmd::namei(fs, "/dev/tty"), 0);

    cmd::extract(fs, dst.dir, opt);
    fs.close();

    auto same = [](const std::string &a, const std::string &b) {
        std::ifstream fa(a, std::ios::binary), fb(b, std::ios::binary);
        std::stringstream sa, sb;
        sa << fa.rdbuf();
        sb << fb.rdbuf();
        return sa.str() == sb.str();
    };

    EXPECT_TRUE(same(src / "etc/passwd", dst / "etc/passwd"));
    EXPECT_TRUE(same(src / "usr/lib/big", dst / "usr/lib/big"));
    EXPECT_TRUE(same(src / "usr/lib/huge", dst / "usr/lib/huge"))
        << "the double-indirect file did not survive the round trip";
    EXPECT_TRUE(same(src / "etc/passwd", dst / "etc/passwd.bak"));

    std::remove(img);
}

//
// Intermediate directories are created on demand, so a manifest need not list
// every level.
//
TEST(Command, CreatesIntermediateDirectories)
{
    Scratch s("cmd_mkdirp");
    const char *img = "cmd_mkdirp.img";
    write_file(s / "f", "x");

    Filesystem fs;
    create_filesystem(fs, img, MDNBLK, 0, NOW);
    cmd::add_file(fs, "/a/b/c/deep", s / "f", 0644, 0, 0, NOW);

    EXPECT_GT(cmd::namei(fs, "/a"), 0);
    EXPECT_GT(cmd::namei(fs, "/a/b"), 0);
    EXPECT_GT(cmd::namei(fs, "/a/b/c"), 0);
    EXPECT_GT(cmd::namei(fs, "/a/b/c/deep"), 0);

    fs.close();
    std::remove(img);
}

//
// Running out of inodes is reported rather than silently reusing one.
//
TEST(Command, ReportsInodeExhaustion)
{
    Scratch s("cmd_noino");
    const char *img = "cmd_noino.img";
    write_file(s / "f", "x");

    Filesystem fs;
    create_filesystem(fs, img, 200, 32, NOW); // one i-list block: 32 inodes

    bool threw = false;
    try {
        for (int i = 0; i < 200; i++)
            cmd::add_file(fs, "/f" + std::to_string(i), s / "f", 0644, 0, 0, NOW);
    } catch (const FsError &) {
        threw = true;
    }
    EXPECT_TRUE(threw) << "should have run out of inodes";

    fs.close();
    std::remove(img);
}
