/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 1999 Robert Nordier. All rights reserved. */

//
// mkfs -- construct a file system.
//
//	/etc/mkfs special nblocks [ ninodes ]
//
// THE FIRST PROGRAM HERE THAT WRITES A RAW DEVICE.  Until this one, kernel/dev/md.c's
// mdwrite() was dead code: df and quot read /dev/rmd0, dd read it and wrote only files,
// and nothing had ever pushed a block the other way.  What that cost is at the foot of
// this comment and in ../mkfs/README.md; the short of it is that the write side obeys the
// same four alignment conditions the read side does (../df/README.md) plus a fifth of its
// own, and that the driver's sector header turns out to be shared between drives.
//
// THE PROTOTYPE FILE IS NOT PORTED, and that is the one deliberate divergence.  v7's mkfs
// took either a proto file -- a boot program, a size, and a recursive listing of files to
// put on the new volume -- or a bare decimal size, and this port takes only the second.
// Four reasons, in the order they bite:
//
//   * There is no boot block to fill.  v7 read a PDP-11 a.out and copied its text and data
//     onto block 0; on this machine SIMH `load's the kernel, so this port has no such block
//     at all and the superblock is block 0.  See bproc(8), which this system also lacks.
//   * cfile() recurses once per directory level with `char db[BSIZE]' and
//     `daddr_t ib[NINDIR]' as automatics -- 1024 words of frame per level against a stack
//     of 4096 words that nothing checks (../README.md SS6).  Three levels of proto and the
//     program is off the end of its own stack with no diagnostic.
//   * Those two buffers are exactly the ones a raw write cannot use, needing to be
//     MDALIGN-aligned statics, which is a thing a recursive function cannot have.
//   * `b6fsutil -n -M manifest' on the host does the same job better, and is how every
//     image in this tree is in fact populated -- ../../scripts/root.manifest is the proto file
//     this system really uses.  There is no proto fixture anywhere in the tree.
//
// mkfs.1m.umm says all of that again, which is ../README.md SS10's rule.
//
// WHAT IS LEFT IS CLOSER TO cmd/fsutil/create.cpp THAN TO v7's mkfs.c.  That file is the
// host's mkfs, and it is this program's oracle: `b6fsutil -n -s N -T t' produces the image
// this produces, byte for byte, and cmd/mkfs/test/ asserts exactly that under b6sim.  So
// where the two could differ, this follows create.cpp -- the free list built descending,
// the free-inode cache seeded descending, no bad-blocks inode, one inode per two blocks --
// and where v7 differs, v7 is wrong for this machine or merely arbitrary.  Three of those
// are worth naming here because a reader who knows v7's mkfs will look for them:
//
//   * NO BAD-BLOCKS INODE.  v7 writes a dummy IFREG inode 1 holding the bad blocks
//     bflist() found.  There are no bad blocks to find (v7's own badblk() returns 0
//     unconditionally), ialloc() refuses to hand out anything below ROOTINO in any case,
//     and the dummy only gives fsck something to be puzzled by.  Inode 1 is left zero.
//   * s_tinode IS SET, NOT ACCUMULATED.  v7 adds NIPB per i-list block and subtracts one
//     per inode written, which here would come to ninodes-1: it counts the bad-blocks
//     inode that this program does not write.  It is ninodes-2 -- everything but inode 1,
//     which cannot be allocated, and the root, which is in use.  It is a one-off
//     arithmetic error in a field the kernel then keeps -- alloc.c maintains s_tinode
//     from whatever this program seeds it with, so the error never washes out -- and
//     fsck would report it against every filesystem this program ever made.
//   * NO FREE-LIST INTERLEAVE.  v7's bflist() lays the free list out in a rotational
//     pattern described by s_m/s_n and the optional `[ m n ]' arguments.  struct filsys
//     has no s_m or s_n in this port (sys/filsys.h says why), the drive is not a moving-head
//     pack whose latency that pattern was hiding, and the pattern does not survive the
//     first churn anyway.  The list is built plainly, descending, and the arguments are gone.
//
// EVERYTHING THIS PROGRAM NAMES IS A FILESYSTEM BLOCK -- 3072 bytes -- and that is a
// deliberate exception to ../README.md SS4, which says a program reports 1024-byte blocks.
// The argument IS s_fsize and the report IS s_isize and an inode count: these are on-disk
// quantities, not measurements, and a mkfs that had to be told 6000 for a drive that holds
// 2000 blocks would be lying about the thing it is writing.  KBPB does not appear in this
// file.  mkfs.1m.umm has a section saying df(1M) will report three times these numbers.
//
// THE SUPERBLOCK IS WRITTEN LAST, and that is the commit point.  Until it lands the volume
// has no magic and sbcheck() will not mount it, so a run that dies partway leaves something
// obviously unfinished rather than something that looks plausible.  That is also what makes
// the size check safe to be a probe (see main()) rather than a duplicated MDNBLK.
//
// THE RAW WRITE, which is what the task was really about.  A transfer through /dev/rmd1
// goes physio() -> mdstrategy() and those two impose the same four conditions on a write
// that ../df/README.md documents for a read:
//
//   * the buffer must start at byte #0 of a word		(physio, kernel/dev/bio.c)
//   * the count must be a whole number of BSIZEs		(MDTRACK == BSIZEW, dev/md.c)
//   * the buffer's word address must be MDALIGN-aligned		 (dev/md.c)
//   * the seek offset must be a multiple of BSIZE	  (physio truncates, silently)
//
// and one more that only a write can break: physio() refuses a transfer whose base is
// below u.u_tsize, so a raw write may never be sourced from the text segment -- from a
// string literal or a const array.  It cannot fire today, b6_prog() producing FMAGIC and
// getxfile() forcing u_tsize to 0 for that magic, and it is a live trap the day anything
// here is linked pure.  README.md is the account.
//
// So there is ONE buffer, `blk', an int array stepped forward to an aligned word at
// startup, and every read and every write names it and nothing else.  sblock is not it:
// a struct filsys cannot be aligned, so committing the superblock copies it into blk word
// by word.  df.c copies the other way for the same reason, and between them the rule is
// that the aligned buffer is the only object either program ever hands to the kernel.
//
// NOT SETUID, and it must not become so: /dev/rmd0 is mode 0600 because that one node is
// every file's contents.  mkfs is root-only and mkfs.1m.umm says so.  ../README.md SS8.
//

// The order here does not matter and cannot be made to: clang-format sorts a block of <>
// includes alphabetically.  Each header includes what it uses; sys/dir.h is the precedent.
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/dir.h>
#include <sys/fblk.h>
#include <sys/filsys.h>
#include <sys/ino.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

// What this file assumes of the layout, asserted rather than re-derived (../README.md,
// task C4).  The headers carry the rest: sys/filsys.h asserts the superblock is exactly
// one block, sys/ino.h that INOPB dinodes tile one, sys/dir.h that DIRPB entries do.
// Restated here are only the ones this file's ARITHMETIC leans on -- itod()/itoo() and
// the `dp + itoo(...)' step, and "two entries and the rest zero is a whole block".
_Static_assert(BSIZE == BSIZEW * NBPW, "a block must be BSIZEW words of NBPW bytes");
_Static_assert(1 + NICFREE <= BSIZEW, "a chain block must fit the block buffer");
_Static_assert(INOPB * sizeof(struct dinode) == BSIZE, "INOPB dinodes must tile a block");
_Static_assert(DIRPB * sizeof(struct direct) == BSIZE, "DIRPB entries must tile a block");
_Static_assert(ROOTINO == 2, "the root is inode 2 and the free-inode seed starts above it");

// mdstrategy()'s half-zone, which is also a block: kernel/dev/md.c refuses a transfer
// whose physical address is not a multiple of it.  A page is PGSZ words and mapping
// preserves the offset within a page, so aligning the virtual address aligns the
// physical one -- provided PGSZ is a multiple of this, which it is.
#define MDALIGN BSIZEW
_Static_assert(PGSZ % MDALIGN == 0, "a page must be a whole number of MDALIGNs");

// The one transfer target, and the only buffer any raw read or write here names.  BSIZEW
// words of it are used; the rest is the slack the alignment step needs, since where bss
// lands is the linker's business and nothing in C can ask for a 512-word boundary.
static int rawbuf[BSIZEW + MDALIGN];
static int *blk;

// The superblock, in core.  Arithmetic only: it is copied into blk to be written.
static struct filsys sblock;

static time_t fstime;
static int fd = -1;
static char *special;

static void rdfs(daddr_t bno);
static void wtfs(daddr_t bno);
static void clrblk(void);
static daddr_t alloc(void);
static void bfree(daddr_t bno);
static daddr_t number(char *s);

int main(int argc, char **argv)
{
    daddr_t nblk, n, dblk;
    int ninodes, iblocks, isize, cached, i;
    struct dinode *dp;
    struct direct *dirp;

    if (argc < 3 || argc > 4) {
        fprintf(stderr, "usage: /etc/mkfs special nblocks [ ninodes ]\n");
        return 1;
    }
    special = argv[1];
    nblk    = number(argv[2]);
    ninodes = argc > 3 ? (int)number(argv[3]) : 0;

    // An `int *' IS a word address on this machine, so the alignment is ordinary
    // arithmetic.  df.c and quot.c step the same way; a loop rather than a mask because
    // it runs once, and because a mask over a negation would depend on how a negative
    // word is spelled here.
    blk = rawbuf;
    while ((int)blk % MDALIGN != 0)
        blk++;

    fstime = time((time_t *)0);

    // create.cpp's wording, verbatim, so that the host tool and this program answer the
    // same way to the same bad input.
    if (nblk < 8) {
        fprintf(stderr, "mkfs: a filesystem needs at least 8 blocks\n");
        return 1;
    }

    // O_RDWR and not v7's creat(): the special may be an ordinary file (it is, under
    // b6sim), and creat() would truncate it.  One descriptor serves rdfs() and wtfs()
    // both, where v7 opened the same name twice.
    fd = open(special, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "mkfs: %s: cannot open\n", special);
        return 1;
    }

    // IS THE DEVICE THAT BIG?  Read the last block before writing the first.  The drive's
    // size is MDNBLK, a constant of kernel/dev/md.c that no header exports to a program,
    // and duplicating it here is exactly what ../README.md forbids -- so ask the device
    // instead: mdstrategy() refuses blkno + wcount/MDTRACK > MDNBLK, so a read of the last
    // block answers the question and costs one exchange.  It catches three things at once:
    // a size larger than the drive, a drive nobody attached, and (under b6sim) a fixture
    // file too short for the size asked.  Nothing has been written when it fails.
    rdfs(nblk - 1);

    // How big the i-list is, and where the data starts.
    //
    // ONE INODE PER TWO BLOCKS by default, which is create.cpp's ratio and not v7's 1:4.
    // A block here is 3072 bytes, six times a PDP-11's, so a file that cost six blocks
    // there costs one here while still costing exactly one inode; the natural ratio is
    // nearer 1:1 than 1:4.  On a 2000-block drive that is 1024 inodes in 32 blocks.
    //
    // s_isize IS THE FIRST DATA BLOCK, not a count of i-list blocks.  ialloc() scans
    // `for (adr = SUPERB+1; adr < s_isize; adr++)' and badblock() rejects `bn < s_isize',
    // so this number bounds a loop in the kernel and one too high is a runaway read
    // rather than a wrong answer.
    //
    //	block 0		superblock (SUPERB) -- there is no boot block
    //	blocks 1..	the i-list
    //	block isize..	data
    if (ninodes <= 0)
        ninodes = (int)(nblk / 2);
    iblocks = (ninodes + INOPB - 1) / INOPB;
    if (iblocks < 1)
        iblocks = 1;
    isize = SUPERB + 1 + iblocks;
    if (isize >= nblk) {
        fprintf(stderr, "mkfs: the i-list does not leave room for any data blocks\n");
        return 1;
    }
    ninodes = iblocks * INOPB; // round up to fill the last i-list block

    // Zero the i-list.  It must start clean: ialloc() decides an inode is free by finding
    // di_mode == 0.  This is also what lets the root inode be written below with a bare
    // wtfs() instead of v7's iput() read-modify-write -- nothing else has touched it since.
    clrblk();
    for (n = SUPERB + 1; n < isize; n++)
        wtfs(n);

    sblock.s_magic  = FS_MAGIC;
    sblock.s_bsize  = BSIZEW;
    sblock.s_inopb  = INOPB;
    sblock.s_naddr  = NADDR;
    sblock.s_isize  = isize;
    sblock.s_fsize  = nblk;
    sblock.s_time   = fstime;
    sblock.s_nfree  = 0;
    sblock.s_ninode = 0;
    sblock.s_tfree  = 0;

    // THE FREE LIST, BUILT DESCENDING.  alloc() pops the superblock's cache from the top,
    // so the LAST block freed is the FIRST one handed out: freeing from the end of the
    // volume down to the first data block leaves the kernel allocating isize, isize+1,
    // isize+2 ... in ascending order.  Build it the other way round and every file the
    // kernel writes runs backwards across the platter.
    for (n = nblk - 1; n >= isize; n--)
        bfree(n);

    // The root directory: `.' and `..' both pointing at the root itself.
    dblk = alloc();
    clrblk();
    dirp              = (struct direct *)blk;
    dirp[0].d_ino     = ROOTINO;
    dirp[0].d_name[0] = '.';
    dirp[1].d_ino     = ROOTINO;
    dirp[1].d_name[0] = '.';
    dirp[1].d_name[1] = '.';
    wtfs(dblk);

    // ... and its inode.  Inode 1 is left with di_mode == 0; see the header.
    clrblk();
    dp = (struct dinode *)blk + itoo(ROOTINO);
    // S_IFDIR and not <sys/inode.h>'s IFDIR: that header is the KERNEL's in-core inode and
    // declares `extern struct inode inode[]'.  quot.c made the same swap for the same reason.
    dp->di_mode    = S_IFDIR | 0777;
    dp->di_nlink   = 2; // `.' and `..', both pointing here
    dp->di_uid     = 0;
    dp->di_gid     = 0;
    dp->di_size    = 2 * (int)sizeof(struct direct);
    dp->di_atime   = fstime;
    dp->di_mtime   = fstime;
    dp->di_ctime   = fstime;
    dp->di_addr[0] = dblk;
    wtfs(itod(ROOTINO));

    // Seed the superblock's free-inode cache, DESCENDING, so that ialloc() -- which pops
    // s_inode[--s_ninode] -- hands out the LOWEST number first and keeps the i-list dense.
    // v7 seeds it ascending and hands out the highest; nothing depends on the order, but a
    // dense i-list reads better in a dump and makes fsck's output stable.  The list starts
    // at ROOTINO+1: inode 1 is not allocatable and inode 2 is the root.
    cached = ninodes - ROOTINO;
    if (cached > NICINOD)
        cached = NICINOD;
    if (cached < 0)
        cached = 0;
    sblock.s_ninode = cached;
    for (i = 0; i < cached; i++)
        sblock.s_inode[i] = ROOTINO + cached - i;

    // Free inodes: everything except inode 1, which cannot be allocated, and the root,
    // which is now in use.  SET and not accumulated -- see the header.
    sblock.s_tinode = ninodes - 2;

    // The commit point.  A struct filsys cannot be an aligned raw target, so it goes out
    // through blk like everything else.  No clrblk() first: the superblock is exactly BSIZEW
    // words -- sys/filsys.h asserts it -- so this copy leaves nothing of the last block behind.
    for (i = 0; i < BSIZEW; i++)
        blk[i] = ((int *)&sblock)[i];
    wtfs(SUPERB);

    printf("mkfs: %s: %d blocks, %d inodes in blocks %d..%d, first data block %d\n", special, nblk,
           ninodes, SUPERB + 1, isize - 1, isize);
    return 0;
}

// One block, into the one aligned buffer.  Whole blocks at block-aligned offsets are the
// only shape a raw transfer here can take; see the header.
//
// errno IS CLEARED FIRST, which is not decoration: a short read is not a failure, so errno
// would otherwise hold whatever the last failing call left there and the diagnostic would
// differ from run to run.  Zero now means "the device had nothing more to give", which is
// exactly what the probe in main() wants to report.
static void rdfs(daddr_t bno)
{
    int n;

    lseek(fd, (off_t)bno * BSIZE, SEEK_SET);
    errno = 0;
    if ((n = read(fd, (char *)blk, BSIZE)) != BSIZE) {
        fprintf(stderr, "mkfs: %s: cannot read block %d\n", special, bno);
        fprintf(stderr, "count = %d; errno = %d\n", n, errno);
        exit(1);
    }
}

// ... and out of it.  An EFAULT here means one of the four conditions in ../df/README.md
// is broken and the word address of blk is the first thing to look at; an EIO means the
// device refused the block number -- the drive is smaller than the size asked for, or
// nobody attached it.
static void wtfs(daddr_t bno)
{
    int n;

    lseek(fd, (off_t)bno * BSIZE, SEEK_SET);
    errno = 0;
    if ((n = write(fd, (char *)blk, BSIZE)) != BSIZE) {
        fprintf(stderr, "mkfs: %s: cannot write block %d\n", special, bno);
        fprintf(stderr, "count = %d; errno = %d\n", n, errno);
        exit(1);
    }
}

static void clrblk(void)
{
    int i;

    for (i = 0; i < BSIZEW; i++)
        blk[i] = 0;
}

// Pop one block off the free list, chaining through a struct fblk when the superblock's
// cache empties.  The kernel's alloc(), less the bad-block loop: there is no corrupt list
// to degrade from at mkfs time, this program having just built it.
static daddr_t alloc(void)
{
    daddr_t bno;
    int i;

    sblock.s_tfree--;
    bno = sblock.s_free[--sblock.s_nfree];
    if (bno == 0) {
        fprintf(stderr, "mkfs: out of free space\n");
        exit(1);
    }
    if (sblock.s_nfree <= 0) {
        rdfs(bno);
        sblock.s_nfree = ((struct fblk *)blk)->df_nfree;
        for (i = 0; i < NICFREE; i++)
            sblock.s_free[i] = ((struct fblk *)blk)->df_free[i];
    }
    return bno;
}

// ... and push one on.  The kernel's free(): when the cache is full it is spilled into the
// block being freed, which becomes the next link of the chain.
//
// THE END-OF-LIST SENTINEL IS PLANTED HERE, on the first call, exactly as the kernel's
// free() plants it -- v7's mkfs instead calls bfree(0) once to get a 0 into s_free[0],
// which is a lie about block 0 and would make this program's first act a claim that the
// superblock is free.
static void bfree(daddr_t bno)
{
    struct fblk *fp;
    int i;

    if (sblock.s_nfree <= 0) {
        sblock.s_nfree   = 1;
        sblock.s_free[0] = 0;
    }
    if (sblock.s_nfree >= NICFREE) {
        // Zero the block before filling in the chain header: a chain block is 321 words
        // of a 512-word block and the other 191 go to the disk either way.
        clrblk();
        fp           = (struct fblk *)blk;
        fp->df_nfree = sblock.s_nfree;
        for (i = 0; i < NICFREE; i++)
            fp->df_free[i] = sblock.s_free[i];
        wtfs(bno);
        sblock.s_nfree = 0;
    }
    sblock.s_free[sblock.s_nfree++] = bno;
    sblock.s_tfree++;
}

static daddr_t number(char *s)
{
    daddr_t n;
    char *p;
    int c;

    n = 0;
    if (*s == '\0')
        goto bad;
    for (p = s; (c = *p) != '\0'; p++) {
        if (c < '0' || c > '9')
            goto bad;
        n = n * 10 + (c - '0');
    }
    return n;

bad:
    fprintf(stderr, "mkfs: %s: bad number\n", s);
    exit(1);
}
