//
// Making a filesystem.
//
// The order things happen in here is the whole of the file, and it is chosen so
// that the result is what a running kernel would have produced.  In particular the
// free list is built BACKWARDS; see below, and see alloc.cpp for why the kernel's
// LIFO cache makes that the right direction.
//
#include "create.h"

#include <ctime>

#include "dir.h"
#include "inode.h"

namespace {

//
// How big to make the i-list.
//
// v7 used one inode per four blocks and RetroBSD one per sixteen.  Neither ratio
// transfers: a block here is 3072 BYTES, six times a PDP-11's, so a file that cost
// six blocks there costs one here while still costing exactly one inode.  A v7
// root-plus-usr distribution is some hundreds of files of which most are under
// 3 Kb, so the natural ratio is nearer 1:1 than 1:4.
//
// One inode per two blocks is the default: on a 2000-block drive that is 1024
// inodes in 32 blocks, 1.6% of the volume, with room for the handful of
// multi-block binaries.  -i overrides it.
//
int64_t ilist_blocks(int64_t nblk, int64_t ninodes)
{
    if (ninodes <= 0)
        ninodes = nblk / 2;

    int64_t blocks = (ninodes + INOPB - 1) / INOPB;
    if (blocks < 1)
        blocks = 1;
    return blocks;
}

} // namespace

void create_filesystem(Filesystem &fs, const std::string &path, int64_t nblk, int64_t ninodes,
                       int64_t now)
{
    if (nblk < 8)
        throw FsError("a filesystem needs at least 8 blocks");
    if (nblk > MDNBLK)
        throw FsError(std::to_string(nblk) + " blocks does not fit an EC-5052 drive (" +
                      std::to_string(MDNBLK) + ")");

    const int64_t iblocks = ilist_blocks(nblk, ninodes);

    //
    // s_isize is the FIRST DATA BLOCK, not a count of i-list blocks.  ialloc()
    // scans `for (adr = SUPERB+1; adr < s_isize; adr++)' and badblock() rejects
    // `bn < s_isize', so this number bounds a loop in the kernel and getting it
    // one too high is a runaway read rather than a wrong answer.
    //
    //   block 0        boot
    //   block 1        superblock (SUPERB)
    //   blocks 2..     the i-list
    //   block isize..  data
    //
    const int64_t isize = 2 + iblocks;
    if (isize >= nblk)
        throw FsError("the i-list does not leave room for any data blocks");

    ninodes = iblocks * INOPB; // round up to fill the last i-list block

    if (now == 0)
        now = int64_t(std::time(nullptr));

    //
    // Zero the whole volume.  The i-list in particular must start clean: ialloc()
    // decides an inode is free by finding di_mode == 0.
    //
    fs.image.create(path, nblk);

    fs.sb        = SuperBlock{};
    fs.sb.magic  = int64_t(FS_MAGIC);
    fs.sb.bsize  = BSIZEW;
    fs.sb.inopb  = INOPB;
    fs.sb.naddr  = NADDR;
    fs.sb.isize  = isize;
    fs.sb.fsize  = nblk;
    fs.sb.time   = now;
    fs.sb.nfree  = 0;
    fs.sb.ninode = 0;
    fs.sb.tfree  = 0;

    //
    // THE FREE LIST, BUILT DESCENDING.
    //
    // alloc() pops the superblock's cache from the top, so the LAST block freed is
    // the FIRST one handed out.  Freeing from the end of the volume down to the
    // first data block therefore leaves the kernel allocating isize, isize+1,
    // isize+2 ... in ascending order.  Build it the other way round and every file
    // the kernel writes runs backwards across the platter.
    //
    // s_nfree starts at 0 rather than being seeded with the end-of-list sentinel:
    // block_free() plants that itself on its first call, exactly as the kernel's
    // free() does, so there is one implementation of it and not two.
    //
    for (int64_t n = nblk - 1; n >= isize; n--)
        fs.block_free(n);

    //
    // The root directory.
    //
    // Inode 1 is deliberately left with di_mode == 0.  v7 puts a dummy "bad
    // blocks" inode there, but ialloc() refuses to hand out any inode below
    // ROOTINO anyway (alloc.c:190), so the dummy buys nothing and only gives fsck
    // something to be confused by.
    //
    Inode root;
    root.get(fs, ROOTINO);
    root.clear();
    root.mode  = IFDIR | 0777;
    root.nlink = 2; // `.' and `..', both pointing here
    root.uid   = 0;
    root.gid   = 0;
    root.atime = now;
    root.mtime = now;
    root.ctime = now;

    dir::make_empty(root, ROOTINO); // `..' in the root is the root
    root.save(true);

    //
    // Seed the superblock's free-inode cache.
    //
    // Descending, so that ialloc() -- which pops s_inode[--s_ninode] -- hands out
    // the LOWEST number first and keeps the i-list dense.  v7 seeds it ascending
    // and hands out the highest; nothing depends on the order, but a dense i-list
    // reads better in a dump and makes fsck's output stable.
    //
    // The list starts at ROOTINO+1: inode 1 is not allocatable and inode 2 is the
    // root.
    //
    const int64_t first_free = ROOTINO + 1;
    int64_t cached           = ninodes - first_free + 1;
    if (cached > NICINOD)
        cached = NICINOD;
    if (cached < 0)
        cached = 0;

    fs.sb.ninode = cached;
    for (int64_t i = 0; i < cached; i++)
        fs.sb.inode[size_t(i)] = first_free + cached - 1 - i;

    //
    // Free inodes: everything except inode 1, which cannot be allocated, and the
    // root, which is now in use.
    //
    fs.sb.tinode = ninodes - 2;

    //
    // The superblock is written LAST, and that is the commit point: until it lands
    // the image has no magic and nothing will mount it, so a tool that dies partway
    // through leaves something that is obviously unfinished rather than something
    // that looks plausible.
    //
    fs.sb.dirty = true;
    fs.sync(true);
}
