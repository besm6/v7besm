/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 1999 Robert Nordier. All rights reserved. */

//
// df -- how many free blocks a filesystem has.
//
// The v7 program, unchanged in what it does: open the raw device, sync, read the
// superblock, and count the free list by popping it one block at a time -- through the
// chain blocks (struct fblk) when the superblock's own cache runs out.  It does NOT read
// s_tfree, but it should eventually -- as the kernel now maintains it and it would be one
// lseek rather than a walk.
//
// A BLOCK IS 1024 BYTES IN WHAT THIS PRINTS, and 3072 in what it counts.  The filesystem's
// block is BSIZE == 3072; this reports KBPB == 3 of them per block (sys/param.h), so that a
// number means something without knowing BSIZE.  Two consequences worth expecting: every
// count is a MULTIPLE OF 3, the smallest thing that can be allocated being one 3072-byte
// block; and a number is half a PDP-11's rather than a sixth, v7's block having been 512
// bytes.  THE MULTIPLY IS AT THE printf and nowhere else -- every count in this file is a
// filesystem block until it is printed.  df.1m says so; ../README.md SS4 is the general rule.
//
// WHAT THE PORT HAD TO CHANGE, beyond the mechanical C11 pass:
//
//  1. `bno<<BSHIFT' does not compile, and that is the point.  There IS no BSHIFT in this
//     port and there cannot be -- BSIZE is 3072 and 3072 is not a power of two --
//     sys/param.h says it outright.  It is a multiply now.
//
//  2. FOUR `%D's, which are not a conversion here.  doprnt.c does not know the PDP-11
//     spelling, and an unknown conversion is echoed verbatim AND consumes no argument, so
//     each one would have printed the two characters `%D' and desynchronised every later
//     conversion in the same format.  All four are `%d'; a long is one word.
//
//  3. THE READ ITSELF, which is the part worth reading README.md for.  A raw transfer
//     through /dev/rmd0 goes physio() -> mdstrategy(), and those two impose four rules v7
//     knows nothing about:
//
//       * the buffer must start at byte #0 of a word     (physio, kernel/dev/bio.c)
//       * the count must be a whole number of BSIZEs     (MDTRACK == BSIZEW, dev/md.c)
//       * the buffer's word address must be MDALIGN-aligned          (dev/md.c)
//       * the seek offset must be a multiple of BSIZE    (physio truncates, silently)
//
//     Break any of the first three and read(2) returns EFAULT; break the fourth and it
//     reads the wrong block and says nothing.  So there is ONE read target here, `blk',
//     an int array stepped forward to an aligned word at startup, and bread() reads whole
//     blocks into it and nothing else.  v7's `struct fblk buf' automatic is gone with it:
//     321 words off the stack, and it could not have been read into anyway, being neither
//     aligned nor a whole block.  README.md is the account.
//
//  4. `sync()' STOPPED BEING DECORATIVE.  v7 called it out of caution; on the raw path it
//     is load-bearing, because this read bypasses the buffer cache that the mounted
//     filesystem is still writing through.
//
//  5. bread()'s `exit(0)' on a short read -- a failure reporting success.  It is exit(1).
//
// NOT SETUID, and it must not become so: /dev/rmd0 is mode 0600 because that one node is
// every file's contents.  df is a root-only program here and df.1m says so.  ../README.md
// SS8 is the rule.
//
// The default device list is one entry, because there is one drive and there are no
// partitions (../../root.manifest).  dargv[0] is the argv[0] placeholder v7's loop needs.
//

// The order here does not matter and cannot be made to: clang-format sorts a block of <>
// includes alphabetically, so the on-disk-layout headers arrive ahead of the sys/param.h
// and sys/types.h they need.  They include what they depend on themselves -- see the note
// at the head of each, and sys/dir.h, which is the precedent.
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/fblk.h>
#include <sys/filsys.h>
#include <sys/param.h>
#include <sys/types.h>
#include <unistd.h>

// What this file assumes of the layout, asserted rather than re-derived (../TODO.md,
// task C4).  The headers carry the rest: sys/filsys.h asserts the superblock is exactly
// one block, sys/fblk.h that a chain block fits inside one.
_Static_assert(BSIZE == BSIZEW * NBPW, "a block must be BSIZEW words of NBPW bytes");
_Static_assert(1 + NICFREE <= BSIZEW, "a chain block must fit the block buffer");
_Static_assert(BSIZE % KBYTE == 0, "a block must be a whole number of reported blocks");

// mdstrategy()'s half-zone, which is also a block: kernel/dev/md.c refuses a transfer
// whose physical address is not a multiple of it.  A page is PGSZ words and mapping
// preserves the offset within a page, so aligning the virtual address aligns the
// physical one -- provided PGSZ is a multiple of this, which it is.
#define MDALIGN BSIZEW
_Static_assert(PGSZ % MDALIGN == 0, "a page must be a whole number of MDALIGNs");

static daddr_t blkno = 1;

static char *dargv[] = { "df", "/dev/rmd0", 0 };

static struct filsys sblock;

static int fi;

// The one read target, and the only buffer any raw read here names.  BSIZEW words of it
// are used; the rest is the slack the alignment step needs, since where bss lands is the
// linker's business and nothing in C can ask for a 512-word boundary.
static int rawbuf[BSIZEW + MDALIGN];
static int *blk;

static void dfree(char *file);
static daddr_t alloc(void);
static void bread(daddr_t bno);

int main(int argc, char **argv)
{
    int i;

    // An `int *' IS a word address on this machine -- lib/test/memt.c casts one the same
    // way -- so the alignment is ordinary arithmetic.  A loop rather than a mask because
    // it runs once, and because a mask over a negation would depend on how a negative
    // word is spelled here.
    blk = rawbuf;
    while ((int)blk % MDALIGN != 0)
        blk++;

    if (argc <= 1) {
        for (argc = 1; dargv[argc]; argc++)
            ;
        argv = dargv;
    }

    for (i = 1; i < argc; i++)
        dfree(argv[i]);
    return 0;
}

static void dfree(char *file)
{
    daddr_t i;
    int j;
    int *sp, *dp;

    fi = open(file, O_RDONLY);
    if (fi < 0) {
        fprintf(stderr, "cannot open %s\n", file);
        return;
    }

    // Load-bearing on the raw path; see the header.
    sync();

    // The superblock is exactly one block, so this is a whole-block read by construction.
    // The copy out of the shared buffer is what lets alloc() read chain blocks into it
    // afterwards without losing the superblock it is walking.  A word loop rather than
    // memcpy(): both objects are word-aligned here, and memcpy would step 3072 fat-pointer
    // bytes to move 512 words.
    bread(SUPERB);
    sp = blk;
    dp = (int *)&sblock;
    for (j = 0; j < BSIZEW; j++)
        dp[j] = sp[j];

    i = 0;
    while (alloc())
        i++;
    // KBPB, because a reported block is 1024 bytes and a filesystem block is three of
    // them (sys/param.h).  The count itself stays in filesystem blocks: it is what alloc()
    // pops and what b6fsutil's pass 4 counts, and the two are diffed against each other.
    printf("%s %d\n", file, i * KBPB);
    close(fi);
}

// Pop one block off the free list, chaining through a struct fblk when the superblock's
// cache empties.  Returns 0 at the end of the list, which is what stops the count.
static daddr_t alloc(void)
{
    int i;
    daddr_t b;
    struct fblk *buf;

    i = --sblock.s_nfree;
    if (i < 0 || i >= NICFREE) {
        printf("bad free count, b=%d\n", blkno);
        return 0;
    }
    b = sblock.s_free[i];
    if (b == 0)
        return 0;
    if (b < sblock.s_isize || b >= sblock.s_fsize) {
        printf("bad free block (%d)\n", b);
        return 0;
    }
    if (sblock.s_nfree <= 0) {
        bread(b);
        buf   = (struct fblk *)blk;
        blkno = b;

        sblock.s_nfree = buf->df_nfree;
        for (i = 0; i < NICFREE; i++)
            sblock.s_free[i] = buf->df_free[i];
    }
    return b;
}

// One block, into the one aligned buffer.  Whole blocks at block-aligned offsets are the
// only shape a raw transfer here can take; see the header.
static void bread(daddr_t bno)
{
    int n;

    lseek(fi, (off_t)bno * BSIZE, SEEK_SET);
    if ((n = read(fi, (char *)blk, BSIZE)) != BSIZE) {
        printf("read error %d\n", bno);
        printf("count = %d; errno = %d\n", n, errno);
        exit(1);
    }
}
