//
// A second reader for the format, written to look like the kernel.
//
// WHY THIS FILE EXISTS.  Every other test in this suite checks b6fsutil against
// itself: it writes an image with fsengine and reads it back with fsengine, so a
// pair of matching bugs -- an encoder and a decoder that are wrong in the same
// direction -- passes cleanly and produces images no kernel can mount.  The
// superblock and dirent tests dodge that by hand-checking raw words, but they can
// only cover a handful of fields.
//
// So: the code below is a deliberately dumb, deliberately DUPLICATE transcription
// of the kernel's own readers -- iinit()/sbcheck() from kernel/alloc.c and
// kernel/main.c, alloc() and ialloc() from kernel/alloc.c, bmap() from
// kernel/subr.c, and namei()'s directory loop from kernel/nami.c.  It is written
// from the kernel sources in the kernel's shape, using struct-offset arithmetic
// over a raw block buffer, and it shares NOTHING with fsengine except the
// constants in fsutil.h.  It does not call Inode, SuperBlock, dir:: or Filesystem.
//
// Two independent implementations agreeing is a far stronger statement than one
// implementation agreeing with itself.  It is also what catches the specific class
// of bug this port is exposed to -- an s_isize off by one, dirent slot arithmetic,
// a chain-block spill -- which is exactly what kernel/TODO.md:493-502 warns has
// never been exercised, since sbcheck() has yet to run for real.
//
// KEEP IT DUMB.  If this file ever starts sharing helpers with fsengine, or gets
// refactored to be elegant, it stops being independent and stops being worth
// anything.
//
#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "command.h"
#include "create.h"

namespace {

constexpr int64_t NOW = 1000000000;

//
// ---------------------------------------------------------------------------
// The machine, as the kernel sees it: a disk of blocks, each 512 words of 48
// bits.  Words are read six bytes at a time, big-endian, straight from the file.
// ---------------------------------------------------------------------------
//
class Disk {
public:
    explicit Disk(const std::string &path) : f(std::fopen(path.c_str(), "rb"))
    {
        if (!f)
            throw std::runtime_error(path + ": cannot open");
    }
    ~Disk()
    {
        if (f)
            std::fclose(f);
    }
    Disk(const Disk &)            = delete;
    Disk &operator=(const Disk &) = delete;

    //
    // bread(): one block into a 512-word buffer, as RAW 48-bit words.
    //
    // Raw, not sign-extended, and the distinction is load-bearing.  A block is
    // just 512 machine words; whether a given word is a signed `int', a packed
    // char array or a bit mask depends entirely on which structure it belongs to.
    // The kernel makes that choice per field, by declaring struct members --
    // di_size is an int, d_name is a char[18] -- and so must this.
    //
    // Getting it wrong here cost an hour: an earlier version masked every word to
    // 41 bits on the way in, on the theory that every field is a BESM-6 `int'.
    // That is true of the superblock and the inode but NOT of a directory name,
    // whose first character occupies bits 48-41 -- so "." (0x2E0000000000) was
    // truncated to zero and every lookup failed.
    //
    void bread(int64_t blkno, uint64_t *buf)
    {
        std::fseek(f, long(blkno * BSIZEW * NBPW), SEEK_SET);

        uint8_t raw[BSIZEW * NBPW];
        if (std::fread(raw, 1, sizeof(raw), f) != sizeof(raw))
            throw std::runtime_error("short read");

        for (int i = 0; i < BSIZEW; i++) {
            uint64_t w = 0;
            for (int b = 0; b < NBPW; b++)
                w = (w << 8) | uint64_t(raw[i * NBPW + b]);
            buf[i] = w;
        }
    }

private:
    std::FILE *f = nullptr;
};

//
// Read one word AS A SIGNED `int': 41 bits, sign in bit 41.  Applied at the point
// of use, to the fields the kernel declares as int -- never wholesale to a block.
// See include/sys/types.h.
//
int64_t sword(uint64_t w)
{
    w &= (1ULL << 41) - 1;
    return (w & (1ULL << 40)) ? int64_t(w) - (int64_t(1) << 41) : int64_t(w);
}

//
// ---------------------------------------------------------------------------
// struct filsys, by word offset.  Written out longhand rather than through a
// helper, because the point is to state the layout a second time.
// ---------------------------------------------------------------------------
//
struct Filsys {
    int64_t s_magic, s_bsize, s_inopb, s_naddr;
    int64_t s_isize, s_fsize, s_time, s_tfree, s_tinode;
    int64_t s_flock, s_ilock, s_fmod, s_ronly;
    int64_t s_nfree;
    int64_t s_free[NICFREE];
    int64_t s_ninode;
    int64_t s_inode[NICINOD];
};

void read_super(Disk &d, Filsys &fp)
{
    uint64_t b[BSIZEW];
    d.bread(1, b); // SUPERB

    fp.s_magic  = sword(b[0]);
    fp.s_bsize  = sword(b[1]);
    fp.s_inopb  = sword(b[2]);
    fp.s_naddr  = sword(b[3]);
    fp.s_isize  = sword(b[4]);
    fp.s_fsize  = sword(b[5]);
    fp.s_time   = sword(b[6]);
    fp.s_tfree  = sword(b[7]);
    fp.s_tinode = sword(b[8]);
    fp.s_flock  = sword(b[9]);
    fp.s_ilock  = sword(b[10]);
    fp.s_fmod   = sword(b[11]);
    fp.s_ronly  = sword(b[12]);
    fp.s_nfree  = sword(b[13]);
    for (int i = 0; i < NICFREE; i++)
        fp.s_free[i] = sword(b[14 + i]);
    fp.s_ninode = sword(b[334]);
    for (int i = 0; i < NICINOD; i++)
        fp.s_inode[i] = sword(b[335 + i]);
}

//
// sbcheck(), kernel/alloc.c:156-180.  Returns 0 if the volume would mount.
//
int sbcheck(const Filsys *fp, std::string &why)
{
    if (fp->s_magic != int64_t(FS_MAGIC)) {
        why = "not a filesystem";
        return 1;
    }
    if (fp->s_bsize != BSIZEW || fp->s_inopb != INOPB || fp->s_naddr != NADDR) {
        why = "filesystem geometry mismatch";
        return 1;
    }
    if (fp->s_isize <= SUPERB || fp->s_isize >= fp->s_fsize) {
        why = "bad filesystem size";
        return 1;
    }
    if (fp->s_nfree < 0 || fp->s_nfree > NICFREE || fp->s_ninode < 0 || fp->s_ninode > NICINOD) {
        why = "bad free count";
        return 1;
    }
    return 0;
}

//
// ---------------------------------------------------------------------------
// struct dinode, sixteen words.  iget()/iexpand(), kernel/iget.c:106-107.
// ---------------------------------------------------------------------------
//
struct Dinode {
    int64_t di_mode, di_nlink, di_uid, di_gid;
    int64_t di_size, di_atime, di_mtime, di_ctime;
    int64_t di_addr[NADDR];
};

//
// itod()/itoo(), sys/param.h:250-253, spelled out.
//
void iget(Disk &d, int64_t ino, Dinode *ip)
{
    const int64_t blk = (ino + 2 * INOPB - 1) >> INOSHIFT;
    const int off     = int((ino + 2 * INOPB - 1) & INOMASK);

    uint64_t b[BSIZEW];
    d.bread(blk, b);

    const uint64_t *dp = &b[off * 16];
    ip->di_mode        = sword(dp[0]);
    ip->di_nlink       = sword(dp[1]);
    ip->di_uid         = sword(dp[2]);
    ip->di_gid         = sword(dp[3]);
    ip->di_size        = sword(dp[4]);
    ip->di_atime       = sword(dp[5]);
    ip->di_mtime       = sword(dp[6]);
    ip->di_ctime       = sword(dp[7]);
    for (int i = 0; i < NADDR; i++)
        ip->di_addr[i] = sword(dp[8 + i]);
}

//
// bmap(), kernel/subr.c:24-124, read-only (rwflg == B_READ), transcribed with the
// kernel's own variables and both of its loops.
//
int64_t bmap(Disk &d, const Dinode *ip, int64_t bn)
{
    int i;
    int j, sh;
    int64_t nb;
    uint64_t bap[BSIZEW];

    if (bn < 0)
        return -1;

    if (bn < NADDR - NLEVEL) {
        i  = int(bn);
        nb = ip->di_addr[i];
        if (nb == 0)
            return -1;
        return nb;
    }

    sh = 0;
    nb = 1;
    bn -= NADDR - NLEVEL;
    for (j = NLEVEL; j > 0; j--) {
        sh += NSHIFT;
        nb <<= NSHIFT;
        if (bn < nb)
            break;
        bn -= nb;
    }
    if (j == 0)
        return -1;

    nb = ip->di_addr[NADDR - j];
    if (nb == 0)
        return -1;

    for (; j <= NLEVEL; j++) {
        d.bread(nb, bap);
        sh -= NSHIFT;
        i  = int((bn >> sh) & NMASK);
        nb = sword(bap[i]);
        if (nb == 0)
            return -1;
    }
    return nb;
}

//
// namei()'s directory loop, kernel/nami.c:145-176.  Works in ENTRY numbers, and
// compares all DIRSIZ characters of the name.
//
int64_t dir_lookup(Disk &d, const Dinode *dp, const char *want)
{
    char u_dbuf[DIRSIZ];
    std::memset(u_dbuf, 0, DIRSIZ);
    std::memcpy(u_dbuf, want, std::min(std::strlen(want), size_t(DIRSIZ)));

    int64_t u_offset = 0;
    uint64_t bp[BSIZEW];
    int64_t have = -1;

    while (u_offset < dp->di_size) {
        const int64_t ent = u_offset / DIRENTSZ;
        const int on      = int(ent & DIRMASK);

        if (on == 0 || have < 0) {
            const int64_t blk = bmap(d, dp, ent >> DIRSHIFT);
            if (blk < 0)
                return 0;
            d.bread(blk, bp);
            have = ent >> DIRSHIFT;
        }

        // struct direct: one word of i-number, three of name.
        const uint64_t *de  = &bp[on * DIRWORDS];
        const int64_t d_ino = sword(de[0]);

        char d_name[DIRSIZ];
        for (int w = 0; w < DIRSIZ / NBPW; w++)
            for (int c = 0; c < NBPW; c++)
                d_name[w * NBPW + c] = char(uint8_t(de[1 + w] >> (40 - 8 * c)));

        u_offset += DIRENTSZ;

        if (d_ino == 0)
            continue;

        int i;
        for (i = 0; i < DIRSIZ; i++)
            if (u_dbuf[i] != d_name[i])
                break;
        if (i == DIRSIZ)
            return d_ino;
    }
    return 0;
}

//
// The rest of namei(): walk a path from the root.
//
int64_t namei(Disk &d, const std::string &path)
{
    int64_t ino = ROOTINO;
    size_t i    = 0;

    while (i < path.size()) {
        while (i < path.size() && path[i] == '/')
            i++;
        const size_t start = i;
        while (i < path.size() && path[i] != '/')
            i++;
        if (i == start)
            break;

        Dinode dp;
        iget(d, ino, &dp);
        if ((dp.di_mode & IFMT) != IFDIR)
            return 0;

        ino = dir_lookup(d, &dp, path.substr(start, i - start).c_str());
        if (ino == 0)
            return 0;
    }
    return ino;
}

//
// alloc(), kernel/alloc.c:36-79, over an in-core copy of the superblock.  The
// chain-block reload is the part worth duplicating: it is where a free list built
// in the wrong order shows up.
//
int64_t alloc(Disk &d, Filsys *fp)
{
    int64_t bno;

    do {
        if (fp->s_nfree <= 0)
            return 0;
        if (fp->s_nfree > NICFREE)
            return 0;
        bno = fp->s_free[--fp->s_nfree];
        if (bno == 0)
            return 0;
    } while (bno < fp->s_isize || bno >= fp->s_fsize); // badblock()

    if (fp->s_nfree <= 0) {
        uint64_t b[BSIZEW];
        d.bread(bno, b);
        fp->s_nfree = sword(b[0]);
        for (int i = 0; i < NICFREE; i++)
            fp->s_free[i] = sword(b[1 + i]);
        if (fp->s_nfree <= 0)
            return 0;
    }
    return bno;
}

//
// Read a whole file the way the kernel's rdwri() would: bmap() per block, byte
// offsets divided by BSIZE because 3072 is not a power of two.
//
std::string read_file(Disk &d, const Dinode *ip)
{
    std::string out;
    uint64_t b[BSIZEW];

    for (int64_t off = 0; off < ip->di_size;) {
        const int64_t lbn = off / BSIZE;
        const int64_t on  = off % BSIZE;
        int64_t n         = BSIZE - on;
        if (n > ip->di_size - off)
            n = ip->di_size - off;

        const int64_t blk = bmap(d, ip, lbn);
        if (blk < 0) {
            out.append(size_t(n), '\0'); // a hole
        } else {
            d.bread(blk, b);
            for (int64_t k = 0; k < n; k++) {
                const int64_t byte = on + k;
                out.push_back(char(uint8_t(b[byte / NBPW] >> (40 - 8 * (byte % NBPW)))));
            }
        }
        off += n;
    }
    return out;
}

//
// ---------------------------------------------------------------------------
// A populated image for the model to read.  Built with fsengine -- that is the
// point: fsengine writes, the model reads.
//
// Built ONCE for the whole suite.  Each of these tests only reads, and the image
// costs 6 Mb of writes plus 2.7 Mb of file data to assemble; doing that per test
// took longer than everything else in the suite put together.
// ---------------------------------------------------------------------------
//
struct Fixture {
    std::string img         = "km_root.img";
    std::string passwd_text = "root::0:0:Charlie Root:/:/bin/sh\n";
    std::string big;
    std::string huge;

    Fixture()
    {
        big.resize(700000);
        for (size_t i = 0; i < big.size(); i++)
            big[i] = char(i * 31 + 7);

        huge.resize(2000000); // 651 blocks: reaches the double indirect
        for (size_t i = 0; i < huge.size(); i++)
            huge[i] = char(i * 17 + 3);

        write("km_passwd", passwd_text);
        write("km_big", big);
        write("km_huge", huge);

        Filesystem fs;
        create_filesystem(fs, img, MDNBLK, 0, NOW);

        cmd::make_directory(fs, "/etc", 0755, 0, 0, NOW);
        cmd::make_directory(fs, "/usr", 0755, 0, 0, NOW);
        cmd::make_directory(fs, "/usr/lib", 0755, 0, 0, NOW);
        cmd::add_file(fs, "/etc/passwd", "km_passwd", 0444, 0, 0, NOW);
        cmd::add_file(fs, "/usr/lib/big", "km_big", 0644, 0, 0, NOW);
        cmd::add_file(fs, "/usr/lib/huge", "km_huge", 0644, 0, 0, NOW);
        cmd::add_hard_link(fs, "/etc/passwd.bak", "/etc/passwd");
        cmd::add_device(fs, "/dev/tty", false, 5, 0, 0620, 0, 0, NOW);
        fs.sync(true);
        fs.close();
    }

    ~Fixture()
    {
        std::remove(img.c_str());
        std::remove("km_passwd");
        std::remove("km_big");
        std::remove("km_huge");
    }

    static void write(const char *path, const std::string &text)
    {
        std::ofstream f(path, std::ios::binary);
        f.write(text.data(), std::streamsize(text.size()));
    }
};

//
// One image for the whole suite.  gtest owns the lifetime; the tests only read.
//
class KernelModel : public ::testing::Test {
protected:
    static Fixture *fx;

    static void SetUpTestSuite()
    {
        if (!fx)
            fx = new Fixture();
    }
    static void TearDownTestSuite()
    {
        delete fx;
        fx = nullptr;
    }
};

Fixture *KernelModel::fx = nullptr;

} // namespace

//
// iinit(): the volume mounts.  This is the check kernel/TODO.md:493-502 says has
// never actually run.
//
TEST_F(KernelModel, VolumeMounts)
{
    Disk d(fx->img);

    Filsys fp;
    read_super(d, fp);

    std::string why;
    EXPECT_EQ(sbcheck(&fp, why), 0) << why;

    EXPECT_EQ(fp.s_magic, int64_t(FS_MAGIC));
    EXPECT_EQ(fp.s_bsize, BSIZEW);
    EXPECT_EQ(fp.s_inopb, INOPB);
    EXPECT_EQ(fp.s_naddr, NADDR);
    EXPECT_EQ(fp.s_time, NOW);
    EXPECT_EQ(fp.s_ronly, 0);
    EXPECT_EQ(fp.s_fmod, 0);
}

//
// The root inode reads back as a directory holding `.' and `..'.
//
TEST_F(KernelModel, RootInode)
{
    Disk d(fx->img);

    Dinode root;
    iget(d, ROOTINO, &root);

    EXPECT_EQ(root.di_mode & IFMT, IFDIR);
    EXPECT_EQ(root.di_mode & 07777, 0777);
    EXPECT_GT(root.di_nlink, 2) << "subdirectories added `..' links";

    EXPECT_EQ(dir_lookup(d, &root, "."), ROOTINO);
    EXPECT_EQ(dir_lookup(d, &root, ".."), ROOTINO);
}

//
// namei() walks the tree the tool built.
//
TEST_F(KernelModel, NameiFindsEverything)
{
    Disk d(fx->img);

    EXPECT_GT(namei(d, "/etc"), 0);
    EXPECT_GT(namei(d, "/etc/passwd"), 0);
    EXPECT_GT(namei(d, "/usr/lib"), 0);
    EXPECT_GT(namei(d, "/usr/lib/big"), 0);
    EXPECT_GT(namei(d, "/usr/lib/huge"), 0);
    EXPECT_GT(namei(d, "/dev/tty"), 0);

    EXPECT_EQ(namei(d, "/nonesuch"), 0);
    EXPECT_EQ(namei(d, "/etc/nonesuch"), 0);

    // `..' really goes back up.
    Dinode lib;
    iget(d, namei(d, "/usr/lib"), &lib);
    EXPECT_EQ(dir_lookup(d, &lib, ".."), namei(d, "/usr"));
}

//
// THE content test: a file written by fsengine reads back byte for byte through
// an independent bmap() -- including one big enough to need the double indirect.
//
TEST_F(KernelModel, FileContentsThroughIndependentBmap)
{
    Disk d(fx->img);

    Dinode ip;

    iget(d, namei(d, "/etc/passwd"), &ip);
    EXPECT_EQ(ip.di_size, int64_t(fx->passwd_text.size()));
    EXPECT_EQ(read_file(d, &ip), fx->passwd_text) << "a one-block file";

    iget(d, namei(d, "/usr/lib/big"), &ip);
    EXPECT_EQ(ip.di_size, int64_t(fx->big.size()));
    EXPECT_GT(ip.di_addr[NADDR - 2], 0) << "228 blocks needs the single indirect";
    EXPECT_EQ(read_file(d, &ip), fx->big) << "through the single indirect";

    iget(d, namei(d, "/usr/lib/huge"), &ip);
    EXPECT_EQ(ip.di_size, int64_t(fx->huge.size()));
    EXPECT_GT(ip.di_addr[NADDR - 1], 0) << "651 blocks needs the double indirect";
    EXPECT_EQ(read_file(d, &ip), fx->huge) << "through the double indirect";
}

//
// A hard link is one inode under two names, and the link count says so.
//
TEST_F(KernelModel, HardLinkIsOneInode)
{
    Disk d(fx->img);

    const int64_t a = namei(d, "/etc/passwd");
    const int64_t b = namei(d, "/etc/passwd.bak");
    EXPECT_EQ(a, b);

    Dinode ip;
    iget(d, a, &ip);
    EXPECT_EQ(ip.di_nlink, 2);
}

//
// A device node's major and minor come out of di_addr[0].
//
TEST_F(KernelModel, DeviceNode)
{
    Disk d(fx->img);

    Dinode ip;
    iget(d, namei(d, "/dev/tty"), &ip);

    EXPECT_EQ(ip.di_mode & IFMT, IFCHR);
    EXPECT_EQ(int(ip.di_addr[0] >> 8), 5);
    EXPECT_EQ(int(ip.di_addr[0] & 0377), 0);
}

//
// THE free-list test: the kernel's own alloc() walks the list this tool built,
// gets ascending blocks, and crosses every chain-block reload without a gap.
//
// This is what proves the build order in create.cpp is right.  A list built the
// other way round passes every test in alloc_test.cpp -- which uses fsengine's own
// allocator -- and fails here.
//
TEST_F(KernelModel, FreeListWalksAscending)
{
    Disk d(fx->img);

    Filsys fp;
    read_super(d, fp);

    std::string why;
    ASSERT_EQ(sbcheck(&fp, why), 0) << why;

    std::set<int64_t> seen;
    int64_t prev  = 0;
    int64_t count = 0;

    for (;;) {
        const int64_t bno = alloc(d, &fp);
        if (bno == 0)
            break;

        ASSERT_GE(bno, fp.s_isize) << "handed out an i-list block";
        ASSERT_LT(bno, fp.s_fsize) << "handed out a block past the volume";
        ASSERT_TRUE(seen.insert(bno).second) << "block " << bno << " handed out twice";

        if (prev)
            ASSERT_EQ(bno, prev + 1) << "allocation " << count << " broke the ascending run";
        prev = bno;
        count++;
    }

    EXPECT_EQ(count, fp.s_tfree) << "alloc() should hand out exactly s_tfree blocks";
    EXPECT_GT(count, NICFREE * 3) << "and cross several chain blocks doing it";
}

//
// Every inode the tool marked in use is reachable by name, and every inode it
// left free really is free.  Catches an i-list that is denser or sparser than
// s_isize claims.
//
TEST_F(KernelModel, IlistAgreesWithTheTree)
{
    Disk d(fx->img);

    Filsys fp;
    read_super(d, fp);

    const int64_t ninodes = (fp.s_isize - SUPERB - 1) * INOPB;
    EXPECT_EQ(ninodes, 1024);

    // Collect every i-number reachable from the root.
    std::set<int64_t> reachable;
    std::vector<std::string> todo = { "" };
    reachable.insert(ROOTINO);

    while (!todo.empty()) {
        const std::string path = todo.back();
        todo.pop_back();

        Dinode dp;
        iget(d, namei(d, path.empty() ? "/" : path), &dp);
        if ((dp.di_mode & IFMT) != IFDIR)
            continue;

        for (int64_t off = 0; off < dp.di_size; off += DIRENTSZ) {
            const int64_t ent = off / DIRENTSZ;
            uint64_t bp[BSIZEW];
            const int64_t blk = bmap(d, &dp, ent >> DIRSHIFT);
            if (blk < 0)
                continue;
            d.bread(blk, bp);

            const uint64_t *de = &bp[(ent & DIRMASK) * DIRWORDS];
            if (de[0] == 0)
                continue;

            char name[DIRSIZ + 1] = {};
            for (int w = 0; w < DIRSIZ / NBPW; w++)
                for (int c = 0; c < NBPW; c++)
                    name[w * NBPW + c] = char(uint8_t(de[1 + w] >> (40 - 8 * c)));

            if (std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0)
                continue;

            reachable.insert(sword(de[0]));
            todo.push_back(path + "/" + name);
        }
    }

    // Every allocated inode in the i-list is reachable, and vice versa.
    for (int64_t ino = ROOTINO; ino <= ninodes; ino++) {
        Dinode ip;
        iget(d, ino, &ip);

        if (ip.di_mode != 0) {
            EXPECT_TRUE(reachable.count(ino))
                << "inode " << ino << " is allocated but not reachable from the root";
            EXPECT_GT(ip.di_nlink, 0) << "inode " << ino;
        } else {
            EXPECT_FALSE(reachable.count(ino))
                << "inode " << ino << " is referenced by a directory but marked free";
        }
    }
}
