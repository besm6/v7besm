//
// The superblock -- struct filsys, include/sys/filsys.h -- as the host sees it.
//
// The kernel's struct IS the on-disk layout, because on the BESM-6 every field is
// one word and the compiler lays them out consecutively.  On the host it cannot
// be: an `int' is four bytes here and six there.  So this is a MIRROR -- host
// storage, host types, no layout significance whatsoever -- and load()/save() do
// the format, field by field, at the word offsets named in filsys.h.
//
// The block is 512 words and the struct occupies all of them.  That is
// load-bearing rather than tidy: the kernel's iinit() copies btow(sizeof(struct
// filsys)) words into a buffer and update() writes BSIZEW words of that buffer
// back, so anything short of a full block puts stale buffer contents on the disk
// at the first sync.  save() writes all 512 words, with s_fill[17] explicitly
// zeroed, so an image written here and an image written by a running kernel agree
// byte for byte.
//
#ifndef B6FSUTIL_SUPERBLOCK_H
#define B6FSUTIL_SUPERBLOCK_H

#include <array>
#include <iosfwd>

#include "fsutil.h"
#include "image.h"

//
// Word offsets, from the comments in include/sys/filsys.h.  Named rather than
// counted, because these offsets are the ABI between this tool and the kernel and
// test/superblock_test.cpp asserts every one of them.
//
enum : int {
    SB_MAGIC  = 0,
    SB_BSIZE  = 1,
    SB_INOPB  = 2,
    SB_NADDR  = 3,
    SB_ISIZE  = 4,
    SB_FSIZE  = 5,
    SB_TIME   = 6,
    SB_TFREE  = 7,
    SB_TINODE = 8,
    SB_FLOCK  = 9,
    SB_ILOCK  = 10,
    SB_FMOD   = 11,
    SB_RONLY  = 12,
    SB_NFREE  = 13,
    SB_FREE   = 14,                 // .. SB_FREE + NICFREE - 1 == 333
    SB_NINODE = SB_FREE + NICFREE,  // 334
    SB_INODE  = SB_NINODE + 1,      // 335 .. 494
    SB_FILL   = SB_INODE + NICINOD, // 495 .. 511
};

static_assert(SB_FILL + 17 == BSIZEW, "the superblock must fill the block exactly");

class SuperBlock {
public:
    int64_t magic  = FS_MAGIC;
    int64_t bsize  = BSIZEW;
    int64_t inopb  = INOPB;
    int64_t naddr  = NADDR;
    int64_t isize  = 0; // FIRST DATA BLOCK, not a count -- see validate()
    int64_t fsize  = 0; // size of the whole volume, in blocks
    int64_t time   = 0;
    int64_t tfree  = 0;
    int64_t tinode = 0;
    int64_t flock  = 0;
    int64_t ilock  = 0;
    int64_t fmod   = 0;
    int64_t ronly  = 0;

    int64_t nfree = 0;
    std::array<int64_t, NICFREE> free{};

    int64_t ninode = 0;
    std::array<int64_t, NICINOD> inode{};

    bool dirty = false;

    void load(Image &img);
    void save(Image &img);

    //
    // sbcheck(), kernel/alloc.c:156-180, transcribed.  Returns true if this
    // superblock would mount; on failure writes the kernel's own diagnostic to
    // `err' so that a rejection here reads the same as a rejection there.
    //
    bool validate(std::ostream &err) const;

    void print(std::ostream &out) const;
};

#endif // B6FSUTIL_SUPERBLOCK_H
