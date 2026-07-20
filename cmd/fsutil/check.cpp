//
// fsck.  See check.h for the shape of the five passes.
//
#include "check.h"

#include <cstring>
#include <set>
#include <vector>

#include "dir.h"
#include "inode.h"

namespace {

//
// A cap on how many times one message is printed.  A thoroughly scrambled image
// can produce a line per block, and 2000 identical complaints are less useful
// than ten and a count.
//
constexpr int MAX_SAME = 10;

} // namespace

void Checker::error(const std::string &msg)
{
    nerror++;
    if (nerror <= MAX_SAME * 10)
        *os << msg << "\n";
    else if (nerror == MAX_SAME * 10 + 1)
        *os << "... further problems suppressed\n";
}

//
// Claim a block for an inode.  The heart of pass 1: a block claimed twice means
// two files share storage, which is the single most destructive thing that can be
// wrong with a filesystem.
//
bool Checker::claim(int64_t bno, int64_t ino, const char *what)
{
    if (bno == 0)
        return true; // a hole, not a claim

    if (bno < fs.sb.isize || bno >= fs.sb.fsize) {
        error("inode " + std::to_string(ino) + ": " + what + " block " + std::to_string(bno) +
              " is outside the data area (" + std::to_string(fs.sb.isize) + ".." +
              std::to_string(fs.sb.fsize - 1) + ")");
        return false;
    }

    if (used[size_t(bno)]) {
        error("inode " + std::to_string(ino) + ": " + what + " block " + std::to_string(bno) +
              " is already claimed by another file");
        return false;
    }

    used[size_t(bno)] = 1;
    return true;
}

//
// Walk an indirect block and everything under it.  `level' is 1 for a block of
// data addresses, 2 for one of single-indirect addresses.  There is no level 3.
//
void Checker::walk_indirect(int64_t bno, int level, int64_t ino)
{
    if (!claim(bno, ino, level == 1 ? "single indirect" : "double indirect"))
        return;

    Block b;
    fs.image.read_block(bno, b);

    for (int i = 0; i < NINDIR; i++) {
        const int64_t nb = from_word(b[size_t(i)]);
        if (nb == 0)
            continue;
        if (level > 1)
            walk_indirect(nb, level - 1, ino);
        else
            claim(nb, ino, "data");
    }
}

//
// Pass 1: every allocated inode's block list.
//
void Checker::pass1_inodes()
{
    const int64_t ninodes = fs.inode_count();

    for (int64_t ino = ROOTINO; ino <= ninodes; ino++) {
        Inode ip;
        ip.get(fs, ino);

        if (ip.mode == 0) {
            //
            // A free inode with a link count is a leftover from an interrupted
            // unlink; harmless but worth saying, since it means the count fields
            // cannot all be trusted.
            //
            if (ip.nlink != 0)
                error("inode " + std::to_string(ino) + ": free but has nlink " +
                      std::to_string(ip.nlink));
            continue;
        }

        allocated[size_t(ino)] = 1;
        links[size_t(ino)]     = int32_t(ip.nlink);
        is_dir[size_t(ino)]    = ip.is_dir() ? 1 : 0;

        if (ip.nlink <= 0)
            error("inode " + std::to_string(ino) + ": allocated but has nlink " +
                  std::to_string(ip.nlink));

        const int64_t fmt = ip.mode & IFMT;
        if (fmt != IFDIR && fmt != IFREG && fmt != IFCHR && fmt != IFBLK && fmt != IFMPC &&
            fmt != IFMPB)
            error("inode " + std::to_string(ino) + ": bad file type in mode 0" +
                  std::to_string(ip.mode));

        //
        // A device has no blocks -- di_addr[0] IS its major/minor, so walking the
        // address array would claim a nonsensical block number.
        //
        if (ip.is_dev())
            continue;

        if (ip.size < 0)
            error("inode " + std::to_string(ino) + ": negative size " + std::to_string(ip.size));

        // The direct blocks, then the two indirects.
        for (int i = 0; i < NADDR - NLEVEL; i++)
            claim(ip.addr[size_t(i)], ino, "data");

        if (ip.addr[NADDR - 2] != 0)
            walk_indirect(ip.addr[NADDR - 2], 1, ino);
        if (ip.addr[NADDR - 1] != 0)
            walk_indirect(ip.addr[NADDR - 1], 2, ino);

        //
        // The size must agree with how far the block list reaches.  A size longer
        // than the mapped blocks is legal -- that is a sparse file -- but a size
        // that would need MORE blocks than the format can address is not.
        //
        const int64_t blocks = (ip.size + BSIZE - 1) / BSIZE;
        const int64_t maxblk = (NADDR - NLEVEL) + NINDIR + int64_t(NINDIR) * NINDIR;
        if (blocks > maxblk)
            error("inode " + std::to_string(ino) + ": size " + std::to_string(ip.size) +
                  " needs more blocks than an inode can address");

        if (ip.is_dir() && ip.size % DIRENTSZ != 0)
            error("inode " + std::to_string(ino) + ": directory size " + std::to_string(ip.size) +
                  " is not a multiple of " + std::to_string(DIRENTSZ));
    }
}

//
// Pass 2: the directory tree.
//
void Checker::walk_directory(int64_t ino, const std::string &path, int64_t parent)
{
    if (seen[size_t(ino)]) {
        error(path + ": directory inode " + std::to_string(ino) +
              " appears in the tree more than once (a loop)");
        return;
    }
    seen[size_t(ino)] = 1;
    names[ino]        = path.empty() ? "/" : path;

    Inode dp;
    dp.get(fs, ino);

    bool saw_dot = false, saw_dotdot = false;

    std::vector<std::pair<std::string, int64_t>> subdirs;

    dir::each(dp, [&](int64_t ent, const DirEntry &e) {
        if (e.ino == 0)
            return; // an unused slot

        const std::string child = path + "/" + e.name;

        if (e.ino < ROOTINO || e.ino > fs.inode_count()) {
            error(child + ": entry " + std::to_string(ent) + " points at i-number " +
                  std::to_string(e.ino) + ", which is outside the i-list");
            return;
        }

        if (std::strcmp(e.name, ".") == 0) {
            saw_dot = true;
            if (e.ino != ino)
                error(child + ": `.' points at inode " + std::to_string(e.ino) + ", not " +
                      std::to_string(ino));
            refs[size_t(e.ino)]++;
            return;
        }
        if (std::strcmp(e.name, "..") == 0) {
            saw_dotdot = true;
            if (e.ino != parent)
                error(child + ": `..' points at inode " + std::to_string(e.ino) + ", not " +
                      std::to_string(parent));
            refs[size_t(e.ino)]++;
            return;
        }

        if (!allocated[size_t(e.ino)]) {
            error(child + ": points at inode " + std::to_string(e.ino) +
                  ", which is not allocated");
            return;
        }

        refs[size_t(e.ino)]++;

        if (is_dir[size_t(e.ino)])
            subdirs.emplace_back(child, e.ino);
    });

    if (!saw_dot)
        error((path.empty() ? "/" : path) + ": has no `.' entry");
    if (!saw_dotdot)
        error((path.empty() ? "/" : path) + ": has no `..' entry");

    for (const auto &sd : subdirs)
        walk_directory(sd.second, sd.first, ino);
}

void Checker::pass2_directories()
{
    if (!allocated[size_t(ROOTINO)]) {
        error("the root inode (" + std::to_string(ROOTINO) + ") is not allocated");
        return;
    }
    if (!is_dir[size_t(ROOTINO)]) {
        error("the root inode is not a directory");
        return;
    }

    // The root's `..' is itself.
    walk_directory(ROOTINO, "", ROOTINO);
}

//
// Pass 3: reference counts against di_nlink.
//
void Checker::pass3_link_counts()
{
    const int64_t ninodes = fs.inode_count();

    for (int64_t ino = ROOTINO; ino <= ninodes; ino++) {
        if (!allocated[size_t(ino)])
            continue;

        if (refs[size_t(ino)] == 0) {
            //
            // Allocated, but nothing points at it.  On a real filesystem fsck
            // moves these to lost+found; here the image is a build artefact, so
            // saying so is enough.
            //
            error("inode " + std::to_string(ino) + ": allocated but unreferenced (orphan)");
            continue;
        }

        if (refs[size_t(ino)] != links[size_t(ino)]) {
            const auto found = names.find(ino);
            error("inode " + std::to_string(ino) +
                  (found == names.end() ? "" : " (" + found->second + ")") + ": link count is " +
                  std::to_string(links[size_t(ino)]) + " but " + std::to_string(refs[size_t(ino)]) +
                  " directory entries point at it");
        }
    }
}

//
// Pass 4: the free list.
//
void Checker::pass4_free_list()
{
    //
    // Walked exactly as alloc() walks it, so that a list this pass accepts is one
    // the kernel can drain -- including the chain-block reload, which is where a
    // free list built in the wrong order goes wrong.
    //
    int64_t nfree = fs.sb.nfree;
    std::vector<int64_t> cache(fs.sb.free.begin(), fs.sb.free.end());

    if (nfree < 0 || nfree > NICFREE) {
        error("superblock: s_nfree is " + std::to_string(nfree) + ", outside 0.." +
              std::to_string(NICFREE));
        return;
    }

    std::set<int64_t> chains_seen; // guards against a chain that loops
    int64_t count = 0;

    for (;;) {
        if (nfree <= 0)
            break;

        const int64_t bno = cache[size_t(--nfree)];
        if (bno == 0)
            break; // the end-of-list sentinel

        if (bno < fs.sb.isize || bno >= fs.sb.fsize) {
            error("free list: block " + std::to_string(bno) + " is outside the data area");
            break;
        }

        if (freed[size_t(bno)]) {
            error("free list: block " + std::to_string(bno) + " appears twice");
            break;
        }
        freed[size_t(bno)] = 1;
        count++;

        if (used[size_t(bno)])
            error("block " + std::to_string(bno) + " is both in use and on the free list");

        if (nfree <= 0) {
            // This block is the next chain block.
            if (!chains_seen.insert(bno).second) {
                error("free list: chain block " + std::to_string(bno) + " revisited (a loop)");
                break;
            }

            Block b;
            fs.image.read_block(bno, b);

            nfree = from_word(b[FB_NFREE]);
            if (nfree < 0 || nfree > NICFREE) {
                error("free list: chain block " + std::to_string(bno) + " has a bad count (" +
                      std::to_string(nfree) + ")");
                break;
            }
            for (int i = 0; i < NICFREE; i++)
                cache[size_t(i)] = from_word(b[FB_FREE + i]);
        }
    }

    //
    // s_tfree is this tool's own bookkeeping rather than part of the format -- the
    // kernel does not maintain it -- so a mismatch is a warning about the figure
    // `-v' prints, not about the filesystem.
    //
    if (count != fs.sb.tfree && opt.verbose)
        *os << "note: s_tfree says " << fs.sb.tfree << " free blocks, the list holds " << count
            << "\n";
}

//
// Pass 5: every block accounted for.
//
void Checker::pass5_accounting()
{
    int64_t lost = 0;

    for (int64_t bno = fs.sb.isize; bno < fs.sb.fsize; bno++) {
        if (!used[size_t(bno)] && !freed[size_t(bno)])
            lost++;
    }

    if (lost > 0)
        error(std::to_string(lost) +
              " block(s) are neither in use nor on the free list (lost space)");
}

int Checker::run(std::ostream &out)
{
    os     = &out;
    nerror = 0;

    //
    // The superblock first: every pass below indexes arrays sized from s_fsize and
    // s_isize, so a garbage superblock has to stop the run rather than be walked.
    //
    if (!fs.sb.validate(out)) {
        out << "superblock is not usable; no further checks possible\n";
        return 1;
    }

    if (fs.sb.fsize > fs.image.nblocks()) {
        out << "superblock says " << fs.sb.fsize << " blocks but the image holds only "
            << fs.image.nblocks() << "\n";
        return 1;
    }

    const size_t nb = size_t(fs.sb.fsize);
    const size_t ni = size_t(fs.inode_count()) + 1;

    used.assign(nb, 0);
    freed.assign(nb, 0);
    refs.assign(ni, 0);
    links.assign(ni, 0);
    allocated.assign(ni, 0);
    is_dir.assign(ni, 0);
    seen.assign(ni, 0);
    names.clear();

    if (opt.verbose)
        out << "** Phase 1 - check blocks and sizes\n";
    pass1_inodes();

    if (opt.verbose)
        out << "** Phase 2 - check pathnames\n";
    pass2_directories();

    if (opt.verbose)
        out << "** Phase 3 - check link counts\n";
    pass3_link_counts();

    if (opt.verbose)
        out << "** Phase 4 - check the free list\n";
    pass4_free_list();

    if (opt.verbose)
        out << "** Phase 5 - check block accounting\n";
    pass5_accounting();

    if (nerror == 0 && opt.verbose) {
        int64_t inuse = 0, free_blocks = 0;
        for (int64_t b = fs.sb.isize; b < fs.sb.fsize; b++) {
            inuse += used[size_t(b)] ? 1 : 0;
            free_blocks += freed[size_t(b)] ? 1 : 0;
        }
        out << inuse << " blocks in use, " << free_blocks << " free\n";
    }

    return nerror;
}
