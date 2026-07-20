//
// A mounted filesystem image: the container, its superblock, and the two
// allocators that sit on top of them.
//
// The allocators are TRANSCRIPTIONS of kernel/alloc.c, not reimplementations.
// That matters more than it looks: the free list is a linked structure whose shape
// is decided by the order things are put into it, and the kernel is the only
// reader that counts.  Anything this tool builds that the kernel would not have
// built is a difference waiting to surface as a corrupted volume three mounts
// later.  Where the kernel does something surprising -- and free() does, twice --
// the surprise is reproduced and commented rather than tidied away.
//
#ifndef B6FSUTIL_FILESYSTEM_H
#define B6FSUTIL_FILESYSTEM_H

#include <string>

#include "fsutil.h"
#include "image.h"
#include "superblock.h"

//
// The free-list chain block, include/sys/fblk.h: one count and NICFREE addresses.
// Its array is the same length as the superblock's by necessity -- the kernel
// wcopy()s between the two sizing the copy from the filsys side, so a mismatch
// would silently overrun one of them.
//
enum : int {
    FB_NFREE = 0,
    FB_FREE  = 1, // .. FB_FREE + NICFREE - 1
};

static_assert(FB_FREE + NICFREE <= BSIZEW, "a chain block must fit one block");

class Filesystem {
public:
    Image image;
    SuperBlock sb;

    void open(const std::string &path, bool for_write);
    void close();

    //
    // Write the superblock back if anything has touched it.  `force' writes it
    // regardless, which is what the end of a create wants.
    //
    void sync(bool force = false);

    //
    // alloc(), kernel/alloc.c:36-79.  Returns the block number; throws FsError
    // when the volume is full, which is the kernel's ENOSPC.
    //
    int64_t block_alloc();

    //
    // free(), kernel/alloc.c:85-118.
    //
    void block_free(int64_t bno);

    //
    // ialloc(), kernel/alloc.c:184-230.  Returns the i-number of a free inode,
    // refilling the superblock's cache from the i-list when it runs dry.  Never
    // returns anything below ROOTINO -- the kernel will not either.
    //
    int64_t inode_alloc();

    //
    // ifree(), kernel/alloc.c.  Puts an i-number back in the cache if there is
    // room; if there is not, it is simply dropped and a later scan will find it.
    //
    void inode_free(int64_t ino);

    // How many inodes the i-list holds, derived from s_isize.
    int64_t inode_count() const { return (sb.isize - SUPERB - 1) * INOPB; }

    //
    // badblock(), kernel/alloc.c:134.  A block is usable if it is past the i-list
    // and inside the volume.
    //
    bool bad_block(int64_t bno) const { return bno < sb.isize || bno >= sb.fsize; }

    Filesystem()                              = default;
    Filesystem(const Filesystem &)            = delete;
    Filesystem &operator=(const Filesystem &) = delete;
};

#endif // B6FSUTIL_FILESYSTEM_H
