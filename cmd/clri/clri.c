/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// clri -- clear an i-node.
//
//	/etc/clri filesystem inumber ...
//
// Task C4e, and the last of the four because it is the only one that ONLY writes.  What it
// exists for is the file that appears in no directory: fsck(1M) reconnects such a thing
// into /lost+found, and clri throws it away instead.  Afterwards its blocks belong to
// nothing, which is what ../icheck/icheck.c calls `missing' -- so the two programs are each
// other's demonstration, and cmd/clri/test asserts exactly that.
//
// THE ARITHMETIC IS THE PORT.  v7 defines `BSIZE 512' for itself, computes the block as
// `((n-1)/NI + 2) * (long)512' with NI = BSIZE/sizeof(struct dinode), and the offset within
// it as `(n-1)%NI'.  All three constants are wrong here and none of them needs to be
// rewritten by hand: <sys/param.h> has itod() and itoo(), which are the same expressions
// with the layout in ONE home (../TODO.md's rule for on-disk constants).  Checked at the
// boundaries -- itod(1) == 2, itoo(1) == 0, itod(INOPB+1) == 3, itoo(INOPB+1) == 0 -- so
// this is a re-spelling and not a change of behaviour.
//
// THE BUFFER IS AN ALIGNED SLOT, not v7's `struct ino buf[NI]'.  A raw transfer through
// /dev/rmd0 goes physio() -> mdstrategy() and wants byte #0 of a word, a whole number of
// BSIZEs, a buffer whose WORD address is a multiple of MDALIGN, and a block-aligned seek;
// ../df/README.md is the account and ../icheck/icheck.c the sibling.  A WRITE has a fifth
// condition, ../mkfs/README.md's -- physio() refuses a base below u.u_tsize -- which cannot
// fire here, the slot being bss.  v7's `char junk[ISIZE]' wrapper existed to avoid naming
// struct dinode; naming it is free.
//
// TWO DIVERGENCES, both in the direction of not destroying a filesystem:
//
//   * THE I-NUMBER IS VALIDATED against the superblock's i-list extent.  v7 seeks wherever
//     atoi() names, so `clri /dev/rmd0 99999' writes 3,072 bytes of zeros over a DATA
//     block -- somebody's file, with no diagnostic.  An i-number outside 1..imax is
//     refused here.  Inode 1 is refused too: it exists and ialloc() can never hand it out.
//     ROOTINO is allowed, clearing the root being a legitimate if drastic thing to want.
//
//   * A HOT ROOT STOPS THE MACHINE, as fsck does.  v7's BUGS section says "if the file is
//     open, clri is likely to be ineffective"; the specific reason on this system is that
//     the kernel holds an in-core copy of the inode and iput() writes it back over what
//     this program just wrote.  So after clearing anything on the mounted root it prints
//     fsck's banner and refuses to return.  israwroot() is fsck's unrawname(): st_rdev
//     cannot answer for the RAW node, which is the one this is normally pointed at.
//
// isnumber() IS RENAMED.  C11 reserves `is' plus a lower-case letter for <ctype.h>, and
// this libc really has that namespace; cmd/chown/chown.c hit the same thing.
//
// NOT SETUID: /dev/rmd0 is mode 0600 because that one node is every file's contents, and a
// setuid clri would hand the whole volume to anybody who could think of an i-number.
// ../README.md SS8.
//

// The order here does not matter and cannot be made to: clang-format sorts a block of <>
// includes alphabetically, so the on-disk-layout headers arrive ahead of the sys/param.h
// and sys/types.h they need.  They include what they depend on themselves.
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/filsys.h>
#include <sys/ino.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// What this file's arithmetic assumes of the layout, asserted rather than re-derived
// (../TODO.md, task C4).  sys/ino.h carries the rest.
_Static_assert(BSIZE == BSIZEW * NBPW, "a block must be BSIZEW words of NBPW bytes");
_Static_assert(INOPB * sizeof(struct dinode) == BSIZE, "INOPB dinodes must tile a block");

// mdstrategy()'s half-zone, which is also a block: kernel/dev/md.c refuses a transfer whose
// physical address is not a multiple of it.  A page is PGSZ words and mapping preserves the
// offset within a page, so aligning the virtual address aligns the physical one.
#define MDALIGN BSIZEW
_Static_assert(PGSZ % MDALIGN == 0, "a page must be a whole number of MDALIGNs");

#define NSLOTS 2 // the superblock, and a block of the i-list

static int rawbuf[NSLOTS * BSIZEW + MDALIGN];
static int *sbslot;
static int *inoslot;

#define sblock (*(struct filsys *)sbslot)

static int fi;
static int status;
static int hotroot;
static int modified;

static ino_t imax;

static int alldigits(const char *s);
static int setup(const char *file);
static void bread(int *buf, daddr_t bno);
static int bwrite(int *buf, daddr_t bno);

int main(int argc, char **argv)
{
    int *p;
    int i, j, k;
    ino_t n;
    struct dinode *itab;

    if (argc < 3) {
        printf("usage: clri filesystem inumber ...\n");
        return 4;
    }

    // An `int *' IS a word address on this machine, so the alignment is ordinary
    // arithmetic; df.c, quot.c, mkfs.c, fsck.c and the other three of task C4e all step
    // the same way.
    p = rawbuf;
    while ((int)p % MDALIGN != 0)
        p++;
    sbslot  = p;
    inoslot = p + BSIZEW;
    itab    = (struct dinode *)inoslot;

    if (setup(argv[1]) == 0)
        return 4;

    // TWO PASSES ON PURPOSE, and v7's one piece of defensive design here: every i-number is
    // checked, and every block it names is read, before anything at all is written.  A run
    // that is going to fail on its fourth argument fails before it has cleared the first
    // three.
    for (i = 2; i < argc; i++) {
        if (!alldigits(argv[i])) {
            printf("%s: is not a number\n", argv[i]);
            status = 1;
            continue;
        }
        n = atoi(argv[i]);
        if (n == 0) {
            printf("%s: is zero\n", argv[i]);
            status = 1;
            continue;
        }
        // Not v7, which seeks wherever it is told; see the head comment.
        if (n < ROOTINO || n > imax) {
            printf("%s: out of range 2..%d\n", argv[i], imax);
            status = 1;
            continue;
        }
        bread(inoslot, itod(n));
    }
    if (status) {
        close(fi);
        return status;
    }

    for (i = 2; i < argc; i++) {
        n = atoi(argv[i]);
        printf("clearing %d\n", n);
        bread(inoslot, itod(n));
        // A word loop over the named struct, not v7's byte loop over a `char junk[]'
        // stand-in: both objects are word-aligned and a dinode is sixteen whole words.
        j = itoo(n);
        for (k = 0; k < (int)(sizeof(struct dinode) / NBPW); k++)
            ((int *)&itab[j])[k] = 0;
        if (!bwrite(inoslot, itod(n)))
            status = 1;
    }
    sync();
    close(fi);

    if (modified && hotroot) {
        // fsck's spin, and for its reason: the kernel's in-core inode is now newer than
        // the disk's and iput() would write it straight back over the cleared one.
        // pause() rather than a busy loop, which would spend a SIMH `step' budget a test
        // may be counting on.
        printf("\n***** BOOT UNIX (NO SYNC!) *****\n");
        for (;;)
            pause();
    }
    return status;
}

//
// Open the device and read its superblock, which is where the i-list extent comes from.
//
static int setup(const char *file)
{
    struct stat st;
    dev_t rootdev;
    int i, n, last;
    char name[64];

    if (stat("/", &st) >= 0) {
        rootdev = st.st_dev;
        if (stat(file, &st) >= 0) {
            // AM I CLEARING AN INODE OF THE FILESYSTEM I AM STANDING ON?  st_rdev cannot
            // answer for the RAW node -- rootdev is makedev(0,0) (kernel/conf.c) and
            // /dev/rmd0's st_rdev is makedev(3,0) -- so the raw name is mapped back to the
            // block one as 4.xBSD's unrawname() does.  fsck.c has the long version, and
            // this is it open-coded rather than a fifth copy of the helper: every
            // comparison is by index, a path being walked and never ordered (SS2).
            if ((st.st_mode & S_IFMT) == S_IFBLK && st.st_rdev == rootdev)
                hotroot++;
            else if ((st.st_mode & S_IFMT) == S_IFCHR) {
                for (n = 0; file[n] != 0 && n < (int)sizeof(name) - 1; n++)
                    name[n] = file[n];
                name[n] = 0;
                last    = 0;
                for (i = 0; i < n; i++)
                    if (name[i] == '/')
                        last = i + 1;
                if (name[last] == 'r') {
                    for (i = last; i < n; i++)
                        name[i] = name[i + 1];
                    if (stat(name, &st) >= 0 && (st.st_mode & S_IFMT) == S_IFBLK &&
                        st.st_rdev == rootdev)
                        hotroot++;
                }
            }
        }
    }

    fi = open(file, O_RDWR);
    if (fi < 0) {
        printf("cannot open %s\n", file);
        return 0;
    }
    if (hotroot)
        printf("CLEARING AN INODE OF THE MOUNTED ROOT\n");

    // Load-bearing on the raw path: this read bypasses the buffer cache the mounted
    // filesystem is still writing through.  ../df/README.md.
    sync();

    bread(sbslot, SUPERB);

    // THE GEOMETRY WORDS, which v7 does not read a superblock at all to look at.  Without
    // them there is nothing to bound an i-number against.  ../fsck/README.md SS1.
    if (sblock.s_magic != FS_MAGIC) {
        printf("%s: not a filesystem\n", file);
        close(fi);
        return 0;
    }
    if (sblock.s_bsize != BSIZEW || sblock.s_inopb != INOPB || sblock.s_naddr != NADDR) {
        printf("%s: filesystem geometry mismatch\n", file);
        printf("bsize %d inopb %d naddr %d; this system wants %d %d %d\n", sblock.s_bsize,
               sblock.s_inopb, sblock.s_naddr, BSIZEW, INOPB, NADDR);
        close(fi);
        return 0;
    }

    // s_isize is the FIRST DATA BLOCK, not a count, so the i-list is blocks SUPERB+1
    // through s_isize-1 and itod() puts inode 1 at the start of SUPERB+1.
    imax = ((ino_t)sblock.s_isize - (SUPERB + 1)) * INOPB;
    if (imax <= 0 || sblock.s_isize >= sblock.s_fsize) {
        printf("Check fsize and isize: %d, %d\n", sblock.s_fsize, sblock.s_isize);
        close(fi);
        return 0;
    }
    return 1;
}

//
// One block, in and out.  Both obey the conditions ../df/README.md lists: a whole BSIZE
// into an MDALIGN-aligned buffer at a block-aligned offset.
//
static void bread(int *buf, daddr_t bno)
{
    int i;

    if (lseek(fi, (off_t)bno * BSIZE, SEEK_SET) >= 0 && read(fi, (char *)buf, BSIZE) == BSIZE)
        return;
    printf("read error %d\n", bno);
    status = 1;
    for (i = 0; i < BSIZEW; i++)
        buf[i] = 0;
}

static int bwrite(int *buf, daddr_t bno)
{
    if (lseek(fi, (off_t)bno * BSIZE, SEEK_SET) >= 0 && write(fi, (char *)buf, BSIZE) == BSIZE) {
        modified = 1;
        return 1;
    }
    printf("write error %d\n", bno);
    return 0;
}

// v7 called this isnumber(); C11 reserves `is' plus a lower-case letter.
static int alldigits(const char *s)
{
    int c;

    if (*s == 0)
        return 0;
    while ((c = *s++) != 0)
        if (c < '0' || c > '9')
            return 0;
    return 1;
}
