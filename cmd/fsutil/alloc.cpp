//
// The block allocator, transcribed from kernel/alloc.c.
//
// THE FREE LIST, and why the order it is built in matters.
//
// The superblock caches NICFREE block numbers inline.  When the cache fills, the
// whole of it is written into the block being freed -- which therefore serves as
// both a chain block and, later, a data block -- and the cache is emptied.  alloc()
// pops from the top of the cache, so the list is LIFO: the LAST block freed is the
// FIRST block allocated.
//
// A mkfs that frees blocks in ascending order therefore hands the kernel a
// descending allocation stream and lays every file backwards across the platter.
// create.cpp frees descending for exactly this reason; see the comment there.
//
// ONE DELIBERATE ADDITION.  s_tfree and s_tinode are maintained here and are NOT
// in the kernel's alloc()/free() -- include/sys/filsys.h calls them dead, and v7
// says so itself.  They are kept because `b6fsutil -v' and fsck want a free-space
// figure that does not require walking the whole chain.  Nothing in the kernel
// reads them back, so a stale value cannot break a mount; treat them as this
// tool's bookkeeping rather than as part of the format.
//
#include "filesystem.h"

void Filesystem::open(const std::string &path, bool for_write)
{
    image.open(path, for_write);
    sb.load(image);
}

void Filesystem::close()
{
    if (image.writable() && sb.dirty)
        sync();
    image.close();
}

void Filesystem::sync(bool force)
{
    if (sb.dirty || force) {
        sb.fmod = 0;
        sb.save(image);
    }
}

//
// alloc(), kernel/alloc.c:36-79.
//
int64_t Filesystem::block_alloc()
{
    int64_t bno;

    //
    // The kernel's do/while over badblock(): a cached entry that is out of range
    // is DISCARDED and the next one tried, rather than failing the allocation.
    // That is how a partially corrupt free list degrades instead of wedging.
    //
    do {
        if (sb.nfree <= 0)
            throw FsError("no space: the free list is exhausted");
        if (sb.nfree > NICFREE)
            throw FsError("bad free count: s_nfree is " + std::to_string(sb.nfree));

        bno      = sb.free[--sb.nfree];
        sb.dirty = true;

        //
        // The end-of-list sentinel.  free() plants a 0 in s_free[0] the first time
        // it is called, so it sits at the bottom of the very first chain block and
        // is the last thing any allocation ever pops.
        //
        if (bno == 0)
            throw FsError("no space: the free list is exhausted");

    } while (bad_block(bno));

    //
    // The cache is now empty, and the block just popped IS the chain block holding
    // the next NICFREE addresses.  Read them back into the superblock before
    // handing the block out -- it is about to be overwritten with file data.
    //
    if (sb.nfree <= 0) {
        Block chain;
        image.read_block(bno, chain);

        sb.nfree = from_word(chain[FB_NFREE]);
        if (sb.nfree < 0 || sb.nfree > NICFREE)
            throw FsError("chain block " + std::to_string(bno) + " has a bad count (" +
                          std::to_string(sb.nfree) + ")");
        for (int i = 0; i < NICFREE; i++)
            sb.free[i] = from_word(chain[FB_FREE + i]);

        if (sb.nfree <= 0)
            throw FsError("no space: the free list is exhausted");
    }

    //
    // The kernel clears the buffer before returning it, so a freshly allocated
    // block reads as zeroes rather than as whatever was last in that buffer.  Doing
    // the same here means a block this tool allocates and only partly fills looks
    // on disk exactly like one the kernel allocated and only partly filled.
    //
    const Block zero{};
    image.write_block(bno, zero);

    sb.tfree--;
    sb.dirty = true;
    return bno;
}

//
// free(), kernel/alloc.c:85-118.
//
void Filesystem::block_free(int64_t bno)
{
    if (bad_block(bno))
        throw FsError("block " + std::to_string(bno) + " is not a data block (i-list ends at " +
                      std::to_string(sb.isize) + ", volume is " + std::to_string(sb.fsize) + ")");

    //
    // First call on a fresh filesystem: plant the end-of-list sentinel.  The kernel
    // does this here rather than at mkfs time, so create.cpp does not seed it
    // either -- it just starts freeing with s_nfree at 0 and lets this run.
    //
    if (sb.nfree <= 0) {
        sb.nfree   = 1;
        sb.free[0] = 0;
    }

    if (sb.nfree >= NICFREE) {
        //
        // Spill the cache into the block being freed.  Note the zero-initialised
        // Block: only 1 + NICFREE of its 512 words are filled in, and the kernel
        // added a clrbuf() here (alloc.c:104-110) precisely because without one the
        // remaining 191 words went to the disk as stale kernel memory on every
        // chain block the filesystem ever grew.  Matching it keeps an image this
        // tool writes byte-comparable with one a running kernel writes.
        //
        Block chain{};
        chain[FB_NFREE] = to_word(sb.nfree);
        for (int i = 0; i < NICFREE; i++)
            chain[FB_FREE + i] = to_word(sb.free[i]);

        image.write_block(bno, chain);
        sb.nfree = 0;
    }

    //
    // ... and the block goes into the cache as well.  So a spilled block is BOTH
    // the chain block and s_free[0] -- it will be handed out again by the alloc()
    // that drains the cache it holds, which reads it back first.  This looks like a
    // bug and is not; it is how v7 avoids needing a block to spare.
    //
    sb.free[sb.nfree++] = bno;
    sb.tfree++;
    sb.dirty = true;
}

//
// ialloc(), kernel/alloc.c:184-240.
//
// The superblock caches NICINOD free i-numbers.  When it runs out, the whole
// i-list is scanned for inodes with di_mode == 0 and the cache refilled -- there
// is no free-inode list on disk, the i-list itself IS the record.  That is why
// create.cpp has to zero the i-list, and why an inode this tool allocates must
// have its mode set before the next allocation, or the scan will hand it out
// twice.
//
int64_t Filesystem::inode_alloc()
{
    for (;;) {
        while (sb.ninode > 0) {
            const int64_t ino = sb.inode[--sb.ninode];
            sb.dirty          = true;

            // The kernel skips anything below ROOTINO rather than trusting the cache.
            if (ino < ROOTINO)
                continue;

            //
            // The cache can be stale -- it is only a hint -- so the inode is read
            // back and rejected if it turned out to be in use after all.
            //
            Block b;
            image.read_block(itod(ino), b);
            if (from_word(b[size_t(itoo(ino)) * 16]) == 0) {
                sb.tinode--;
                return ino;
            }
        }

        //
        // Cache empty: scan the i-list.  `ino' starts at 1 because block 2 slot 0
        // is inode 1 -- see itod()/itoo().
        //
        int64_t ino    = 1;
        bool found_any = false;

        for (int64_t adr = SUPERB + 1; adr < sb.isize; adr++) {
            Block b;
            image.read_block(adr, b);

            for (int i = 0; i < INOPB; i++, ino++) {
                if (ino < ROOTINO)
                    continue;
                if (from_word(b[size_t(i) * 16]) != 0)
                    continue; // di_mode is set: in use

                sb.inode[size_t(sb.ninode++)] = ino;
                found_any                     = true;
                if (sb.ninode >= NICINOD)
                    break;
            }
            if (sb.ninode >= NICINOD)
                break;
        }

        if (!found_any)
            throw FsError("out of inodes: the i-list is full (" + std::to_string(inode_count()) +
                          " inodes)");

        //
        // The scan fills the cache ASCENDING, so the pop above takes the highest
        // number first.  create.cpp seeds it descending to get the opposite, but
        // once the filesystem is in use this is the kernel's own behaviour and
        // there is no reason to differ from it.
        //
        sb.dirty = true;
    }
}

void Filesystem::inode_free(int64_t ino)
{
    if (ino < ROOTINO)
        throw FsError("inode " + std::to_string(ino) + " is not allocatable");

    //
    // If the cache is full the number is simply dropped: the i-list is the real
    // record, so the next scan will find it again.  The kernel does the same.
    //
    if (sb.ninode < NICINOD)
        sb.inode[size_t(sb.ninode++)] = ino;

    sb.tinode++;
    sb.dirty = true;
}
