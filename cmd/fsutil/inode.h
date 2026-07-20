//
// The inode -- struct dinode, include/sys/ino.h -- as the host sees it.
//
// Sixteen words: eight of metadata, then eight disk addresses, so INOPB of them
// tile a block exactly.  Like the superblock this is a MIRROR; get()/save() do the
// format at the word offsets ino.h names.
//
// TWO THINGS THAT ARE EASY TO GET WRONG HERE.
//
//   di_size IS IN BYTES.  Not words, not blocks.  And a block is 3072 bytes,
//   which is not a power of two -- so a byte offset converts to a block number by
//   DIVISION, never by a shift.  include/sys/param.h deleted BSHIFT/BMASK
//   outright and explains at length that carrying the PDP-11's pair into this port
//   made every byte-offset conversion in the kernel silently wrong by a factor of
//   six.  The RetroBSD source this is ported from shifts everywhere; none of those
//   shifts survived the port.
//
//   THERE IS NO TRIPLE INDIRECT.  NLEVEL is 2: addr[0..5] direct, addr[6] single,
//   addr[7] double.  The third level is not merely omitted, it is unreachable --
//   one EC-5052 drive is 2000 blocks and the single indirect alone already spans
//   518.  The BSD original walks TRIPLE..0 over 7 addresses; that loop is deleted
//   rather than adapted.
//
#ifndef B6FSUTIL_INODE_H
#define B6FSUTIL_INODE_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "filesystem.h"

//
// Word offsets within a dinode, from include/sys/ino.h.  The 8/8 split is
// deliberate there: `dp + 8' is the address array, at a power-of-two offset.
//
enum : int {
    DI_MODE  = 0,
    DI_NLINK = 1,
    DI_UID   = 2,
    DI_GID   = 3,
    DI_SIZE  = 4,
    DI_ATIME = 5,
    DI_MTIME = 6,
    DI_CTIME = 7,
    DI_ADDR  = 8, // .. 15
    DI_WORDS = 16,
};

static_assert(DI_ADDR + NADDR == DI_WORDS, "the address array must end the inode");
static_assert(DI_WORDS * INOPB == BSIZEW, "INOPB inodes must tile a block");

class Inode {
public:
    Filesystem *fs = nullptr;
    int64_t number = 0;
    bool dirty     = false;

    int64_t mode  = 0;
    int64_t nlink = 0;
    int64_t uid   = 0;
    int64_t gid   = 0;
    int64_t size  = 0; // BYTES
    int64_t atime = 0;
    int64_t mtime = 0;
    int64_t ctime = 0;
    std::array<int64_t, NADDR> addr{};

    //
    // Read inode `ino' out of the i-list.  Inode 1 is block 2 slot 0; see itod()
    // and itoo() in fsutil.h.
    //
    void get(Filesystem &f, int64_t ino);

    // Write it back, if anything has changed or `force'.
    void save(bool force = false);

    // Zero everything but the number: a fresh, unallocated inode.
    void clear();

    //
    // bmap(), kernel/subr.c:24-124, transcribed.  Maps a logical block number to
    // a physical one.  Returns -1 when the block is not present and `allocate' is
    // false -- a HOLE, which is a legitimate part of a sparse file, not an error.
    // Throws when the block number is out of the file's reach entirely (EFBIG).
    //
    int64_t bmap(int64_t lbn, bool allocate);

    //
    // Byte-granular file I/O.  read() stops at end-of-file and returns how much it
    // got; a hole reads as zeroes, as it does through the kernel.
    //
    int64_t read(int64_t off, uint8_t *buf, int64_t nbytes);
    void write(int64_t off, const uint8_t *buf, int64_t nbytes);

    // Release every block and set the size to zero.
    void truncate();

    bool is_dir() const { return (mode & IFMT) == IFDIR; }
    bool is_reg() const { return (mode & IFMT) == IFREG; }
    bool is_dev() const { return (mode & IFMT) == IFCHR || (mode & IFMT) == IFBLK; }
    bool is_allocated() const { return mode != 0; }

    //
    // For a device file, addr[0] doubles as di_rdev -- ino.h says so, and it is
    // addr[0], NOT the addr[1] the RetroBSD source uses.
    //
    int64_t rdev() const { return addr[0]; }
    void set_rdev(int64_t dev)
    {
        addr[0] = dev;
        dirty   = true;
    }

private:
    void free_indirect(int64_t bno, int level);
};

#endif // B6FSUTIL_INODE_H
