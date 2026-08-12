/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 1999 Robert Nordier. All rights reserved. */

//
// icheck -- file system storage consistency check, and free-list salvage.
//
//	/etc/icheck [ -s ] [ -m ] [ -d ] [ -b numbers ] [ filesystem ]
//
// Task C4e, the first of the four one-job tools fsck(1M) grew out of.  It answers the
// question quot(1) cannot -- quot divides di_size, so it counts a file's holes and misses
// its indirect blocks -- by walking every address in every inode and marking a bit map,
// then walking the free list and cross-checking the two.  What is left over is `missing':
// space that is in no file and on no free list.
//
// THE READ PATH IS df(1)'s, and ../df/README.md is the thing to read before touching
// bread() below: a raw transfer through /dev/rmd0 goes physio() -> mdstrategy() and those
// two impose four conditions v7 knows nothing about -- byte #0 of a word, a whole number of
// BSIZEs, a buffer whose WORD address is a multiple of MDALIGN, and a block-aligned seek.
// Three are EFAULT; the fourth is silent.  With -s there is a fifth, ../mkfs/README.md's:
// physio() refuses a base below u.u_tsize, so a raw write may never be sourced from the
// text.  Every buffer here is bss, so it cannot fire.
//
// v7 READ THE I-LIST SIXTEEN BLOCKS AT A TIME (`NI 16', `struct dinode itab[INOPB*NI]' --
// 8,192 words of bss here) and it is not an optimisation that can be kept: a raw transfer
// is one block, so the sweep is quot.c's and fsck.c's, one block re-fetched when itod()
// changes.  The block buffer IS the i-node table, INOPB of them tiling it exactly.
//
// FOUR LEVELS OF INDIRECTION BECAME TWO, and it deletes more than it looks like.  NADDR is
// 8 with NLEVEL 2 -- six direct, one single indirect, one double, and no triple -- so v7's
// `i < NADDR-3' would have walked direct block di_addr[5] as an indirect block, and its
// three nested loops over ind1/ind2/ind3 are a recursion over NLEVEL now.  That also takes
// 1,536 words of automatic daddr_t[NINDIR] off a stack of 4,096 that nothing checks
// (../README.md SS6).
//
// ONE INDIRECT BUFFER PER LEVEL, WHICH IS NOT WHAT fsck DOES, and the difference is worth
// knowing before anyone tidies it.  fsck shares ONE indirect buffer and re-fetches it on
// every iteration, because its DATA walk re-enters iblock() through dirscan/pass2/descend
// and two walks of the same level are live at once.  Nothing here calls back into the walk:
// ckindir() is a closed nested loop, so a buffer per level is safe -- and the shared one
// would be actively wrong, re-reading the outer indirect block on all NINDIR inner
// iterations.  NLEVEL buffers, indexed by level, so retuning NLEVEL cannot leave it behind.
//
// A BLOCK IS 1024 BYTES IN WHAT THIS PRINTS, and 3072 in what it counts -- KBPB of them per
// filesystem block (sys/param.h), so a number means something without knowing BSIZE, and
// so `free' can be held against df(1M)'s and fsck(1M)'s.  THE MULTIPLY IS AT THE printf and
// nowhere else (../README.md SS4).  A block NUMBER is not a measurement and is never
// converted: `%d bad', `%d dup', `%d arg', the per-block `%d missing' under -m and the
// chain-block number in a free-list diagnostic are all on-disk quantities, and so are the
// s_isize/s_fsize a size check prints -- those DESCRIBE the volume, which is mkfs's
// exception to the same rule.  icheck.1m.umm has the section SS4 requires.
//
// THREE UPSTREAM BUGS, fixed rather than carried:
//
//   * `n = ndirect + nindir + niindir + niindir' -- niindir added twice and niiindir never,
//     so the `used' total was wrong on any filesystem with two levels of indirection.
//
//   * `default:' in the option switch printed "Bad flag" and then fell through into
//     check(*argv), so `icheck -q' also said "cannot open -q".
//
//   * the -b loop can leave i == NB and then writes blist[i], one past a NB-element array.
//     The array is NB+1 long here and the loop is bounded.
//
// AND ONE DELIBERATE DIVERGENCE beyond the memory management: v7 degrades to `duplicates
// unchecked' when it cannot get the block map, silently turning -s into a no-op and icheck
// into a program that does not check.  A failure is a refusal here, which is the same call
// fsck.c made about its four maps.
//
// -s IS THE ONLY THING HERE THAT WRITES, and what it writes is fsck's phase 6 rather than
// v7's: struct filsys has no s_m/s_n, there is no rotational interleave to lay down and no
// moving-head pack whose latency it was hiding, so the list is rebuilt plainly and
// descending -- which is how ../mkfs/mkfs.c builds one, so a salvaged volume and a fresh
// one have the same shape.  That deletes makefree()'s flg[500]+adr[500], 584 words of
// frame.  It also has to write s_tinode CORRECTLY, which v7 does not: v7 sets it to zero,
// harmlessly, because nothing in that system maintained the field.  kernel/alloc.c
// maintains it here and cmd/fsutil/check.cpp faults an image on it, so a volume salvaged
// v7's way would come back broken.  The formula is fsck.c's, inode 1 and all.
//
// NOT SETUID, and it must not become so: /dev/rmd0 is mode 0600 because that one node is
// every file's contents, and -s rewrites the free list of the whole volume.  ../README.md
// SS8 is the rule; ../fsck's and ../mkfs's stanzas in ../../root.manifest are the precedent.
//

// The order here does not matter and cannot be made to: clang-format sorts a block of <>
// includes alphabetically, so the on-disk-layout headers arrive ahead of the sys/param.h
// and sys/types.h they need.  They include what they depend on themselves -- see the note
// at the head of each, and sys/dir.h, which is the precedent.
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/fblk.h>
#include <sys/filsys.h>
#include <sys/ino.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

// What this file's arithmetic assumes of the layout, asserted rather than re-derived
// (../README.md, task C4).  The headers carry the rest.
_Static_assert(BSIZE == BSIZEW * NBPW, "a block must be BSIZEW words of NBPW bytes");
_Static_assert(BSIZE % KBYTE == 0, "a block must be a whole number of reported blocks");
_Static_assert(1 + NICFREE <= BSIZEW, "a chain block must fit the block buffer");
_Static_assert(INOPB * sizeof(struct dinode) == BSIZE, "INOPB dinodes must tile a block");
_Static_assert(NINDIR * sizeof(daddr_t) == BSIZE, "NINDIR addresses must tile a block");
_Static_assert(NADDR > NLEVEL, "an inode must have at least one direct block");

// mdstrategy()'s half-zone, which is also a block: kernel/dev/md.c refuses a transfer whose
// physical address is not a multiple of it.  A page is PGSZ words and mapping preserves the
// offset within a page, so aligning the virtual address aligns the physical one.
#define MDALIGN BSIZEW
_Static_assert(PGSZ % MDALIGN == 0, "a page must be a whole number of MDALIGNs");

#define NO  0
#define YES 1

#define NB     10 // -b block numbers remembered
#define BITSPB 8  // bits per byte in the block map

// The five block buffers, cut from one aligned array: the superblock, a block of the
// i-list, NLEVEL indirect blocks and a free-list chain block.
#define NSLOTS (3 + NLEVEL)

static int rawbuf[NSLOTS * BSIZEW + MDALIGN];
static int *sbslot;
static int *inoslot;
static int *indslot[NLEVEL];
static int *fbslot;

// The superblock lives IN an aligned slot rather than in a struct of its own, so that -s
// can write it back without a copy: a bss `struct filsys' is not MDALIGN-aligned and could
// not be the source of a raw write.  fsck.c's `superblk' macro, for the same reason.
#define sblock (*(struct filsys *)sbslot)

// What a block reached through i levels of indirection is called in a diagnostic.  Indexed
// by DEPTH FROM THE INODE, which is v7's `i='/`ii=' sense: the block di_addr names is the
// first indirect however deep the chain below it goes.
_Static_assert(NLEVEL == 2, "the class names below name one level of indirection each");
static const char *indclass[NLEVEL] = { "1st indirect", "2nd indirect" };
static const char *datclass[NLEVEL] = { "data (large)", "data (huge)" };

static char *dargv[] = { "/dev/rmd0", 0 };

static daddr_t blist[NB + 1];
static char *dupmap;
static off_t bmapsz;

static int sflg; // -s: salvage the free list
static int mflg; // -m: name every missing block
static int dflg; // -d: no duplicate checking, and so no block map

static int fi; // the device: one descriptor, O_RDWR under -s
static int hotroot;
static int modified;
static int nerror;

static ino_t inum; // the inode being walked
static ino_t imax; // i-numbers on the volume
static daddr_t fmin;
static daddr_t fmax;

static ino_t nrfile, ndfile, nbfile, ncfile;
static ino_t nfiles; // allocated inodes seen; s_tinode is derived from it
static daddr_t ndirect, nindir, niindir;
static daddr_t nfree, ndup;

#define howmany(x, y) (((x) + ((y) - 1)) / (y))

static void check(char *file);
static int setup(char *file);
static void pass1(struct dinode *ip);
static void ckindir(daddr_t blk, int levels, int depth);
static int chk(daddr_t bno, const char *s);
static int duped(daddr_t bno);
static daddr_t alloc(void);
static void makefree(void);
static void bread(int *buf, daddr_t bno);
static void bwrite(int *buf, daddr_t bno);
static void clearwords(int *p, int n);
static int israwroot(const char *dev, dev_t rootdev);

int main(int argc, char **argv)
{
    int *p;
    int i, n, nchecked;

    // The five block buffers.  An `int *' IS a word address on this machine, so the
    // alignment is ordinary arithmetic; a loop rather than a mask because it runs once and
    // because a mask over a negation would depend on how a negative word is spelled here.
    // df.c, quot.c, mkfs.c and fsck.c all step the same way.
    p = rawbuf;
    while ((int)p % MDALIGN != 0)
        p++;
    sbslot  = p;
    inoslot = p + BSIZEW;
    for (i = 0; i < NLEVEL; i++)
        indslot[i] = p + (2 + i) * BSIZEW;
    fbslot = p + (2 + NLEVEL) * BSIZEW;

    blist[0] = -1;
    nchecked = 0;

    while (--argc > 0) {
        argv++;
        if (**argv == '-') {
            switch ((*argv)[1]) {
            case 'd':
                dflg++;
                continue;
            case 'm':
                mflg++;
                continue;
            case 's':
                sflg++;
                continue;
            case 'b':
                for (i = 0; i < NB && argc > 1; i++) {
                    n = atoi(argv[1]);
                    if (n == 0)
                        break;
                    blist[i] = n;
                    argv++;
                    argc--;
                }
                blist[i] = -1;
                continue;
            default:
                // v7 fell through into check() here and then complained that it could not
                // open the flag.
                printf("Bad flag %c\n", (*argv)[1]);
                nerror |= 04;
                continue;
            }
        }
        check(*argv);
        nchecked++;
    }

    if (nchecked == 0) {
        // v7's DESCRIPTION promises "a set of default file systems"; there is one drive
        // here and no partitions, so the set is one entry -- quot.c's dargv[].  (df's is
        // gone: it takes its default from the kernel's own mount table now.)
        // A SALVAGE is aimed deliberately, though, and is never defaulted: fsck's rule.
        if (sflg) {
            printf("usage: icheck -s filesystem\n");
            return 4;
        }
        check(dargv[0]);
    }
    return nerror;
}

//
// One filesystem: sweep the i-list, then either salvage the free list or walk it and
// report.
//
static void check(char *file)
{
    struct dinode *itab;
    daddr_t d, n, iblk;
    ino_t i;

    if (setup(file) == NO)
        return;

    iblk = -1;
    itab = (struct dinode *)inoslot;
    for (inum = 1; inum <= imax; inum++) {
        if (itod(inum) != iblk) {
            iblk = itod(inum);
            bread(inoslot, iblk);
        }
        pass1(&itab[itoo(inum)]);
    }

    if (sflg) {
        makefree();
        close(fi);
        free(dupmap);
        dupmap = NULL;
        if (modified && hotroot) {
            // fsck's spin, and for its reason: the kernel's in-core superblock is now
            // older than the disk's, and letting it be written back would undo the
            // salvage.  pause() rather than a busy loop, which would spend a SIMH `step'
            // budget a test may be counting on.
            printf("\n***** BOOT UNIX (NO SYNC!) *****\n");
            for (;;)
                pause();
        }
        return;
    }

    nfree = 0;
    while ((n = alloc()) != 0) {
        if (chk(n, "free"))
            break;
        nfree++;
    }
    close(fi);

    i = nrfile + ndfile + ncfile + nbfile;
    printf("files %6d (r=%d,d=%d,b=%d,c=%d)\n", i, nrfile, ndfile, nbfile, ncfile);

    // KBPB at the printf and nowhere else: every counter above holds filesystem blocks.
    n = ndirect + nindir + niindir;
    printf("used %7d (i=%d,ii=%d,d=%d)\n", n * KBPB, nindir * KBPB, niindir * KBPB, ndirect * KBPB);
    printf("free %7d\n", nfree * KBPB);

    if (!dflg) {
        n = 0;
        for (d = fmin; d < fmax; d++)
            if (!duped(d)) {
                // A block NUMBER is an on-disk quantity, not a measurement: no KBPB here.
                if (mflg)
                    printf("%d missing\n", d);
                n++;
            }
        printf("missing%5d\n", n * KBPB);
    }
    free(dupmap);
    dupmap = NULL;
}

//
// Open the device, read its superblock and size the block map from it.
//
static int setup(char *file)
{
    struct stat st;
    dev_t rootdev;

    hotroot  = 0;
    modified = 0;
    nrfile = ndfile = nbfile = ncfile = 0;
    nfiles                            = 0;
    ndirect = nindir = niindir = 0;
    nfree = ndup = 0;

    if (stat("/", &st) < 0) {
        printf("cannot stat /\n");
        nerror |= 04;
        return NO;
    }
    rootdev = st.st_dev;

    if (stat(file, &st) < 0) {
        printf("cannot stat %s\n", file);
        nerror |= 04;
        return NO;
    }
    // AM I LOOKING AT THE FILESYSTEM I AM STANDING ON?  st_rdev cannot answer for the RAW
    // node, which is the one this program is normally pointed at -- rootdev is
    // makedev(0,0) (kernel/conf.c) and /dev/rmd0's st_rdev is makedev(3,0) -- so the raw
    // name is mapped back to the block one as 4.xBSD's unrawname() does.  fsck.c has the
    // long version of this comment.
    if ((st.st_mode & S_IFMT) == S_IFBLK && st.st_rdev == rootdev)
        hotroot++;
    else if ((st.st_mode & S_IFMT) == S_IFCHR && israwroot(file, rootdev))
        hotroot++;

    fi = open(file, sflg ? O_RDWR : O_RDONLY);
    if (fi < 0) {
        printf("cannot open %s\n", file);
        nerror |= 04;
        return NO;
    }
    printf("%s:\n", file);
    if (hotroot && sflg)
        printf("SALVAGING THE MOUNTED ROOT\n");

    // Load-bearing on the raw path: this read bypasses the buffer cache the mounted
    // filesystem is still writing through.  ../df/README.md.
    sync();

    bread(sbslot, SUPERB);

    // THE GEOMETRY WORDS FIRST, which v7's icheck has none of to look at.  sbcheck()
    // (kernel/alloc.c) refuses to mount a superblock that fails these, cmd/fsutil/check.cpp
    // refuses to check one and fsck(1M) refuses to repair one; a program that walked it
    // anyway would be the odd one out.  It is also what v7's BUGS section means by
    // "believes even preposterous super-blocks".
    if (sblock.s_magic != FS_MAGIC) {
        printf("%s: not a filesystem\n", file);
        close(fi);
        nerror |= 04;
        return NO;
    }
    if (sblock.s_bsize != BSIZEW || sblock.s_inopb != INOPB || sblock.s_naddr != NADDR) {
        printf("%s: filesystem geometry mismatch\n", file);
        printf("bsize %d inopb %d naddr %d; this system wants %d %d %d\n", sblock.s_bsize,
               sblock.s_inopb, sblock.s_naddr, BSIZEW, INOPB, NADDR);
        close(fi);
        nerror |= 04;
        return NO;
    }

    // s_isize is the FIRST DATA BLOCK, not a count, so the i-list is blocks SUPERB+1
    // through s_isize-1 and itod() puts inode 1 at the start of SUPERB+1.
    imax = ((ino_t)sblock.s_isize - (SUPERB + 1)) * INOPB;
    fmin = (daddr_t)sblock.s_isize;
    fmax = sblock.s_fsize;
    if (fmin >= fmax || imax <= 0) {
        // s_isize and s_fsize DESCRIBE the volume; they are not measurements, so they are
        // printed exactly as they stand on the disk.
        printf("Check fsize and isize: %d, %d\n", sblock.s_fsize, sblock.s_isize);
        close(fi);
        nerror |= 04;
        return NO;
    }

    if (!dflg) {
        bmapsz = howmany(fmax - fmin, BITSPB);
        dupmap = calloc((unsigned)bmapsz, 1);
        if (dupmap == NULL) {
            // v7 carried on with `duplicates unchecked', which quietly turns -s into a
            // no-op.  A refusal is fsck's call and is the honest one.
            printf("Can't get memory\n");
            close(fi);
            nerror |= 04;
            return NO;
        }
    } else if (sflg) {
        printf("-d and -s are incompatible: a salvage needs the block map\n");
        close(fi);
        nerror |= 04;
        return NO;
    }
    return YES;
}

//
// One inode: count it by type, then claim every block it reaches.
//
static void pass1(struct dinode *ip)
{
    daddr_t iaddr[NADDR];
    int i, mode;

    mode = ip->di_mode & S_IFMT;
    if (mode == 0)
        return;

    // An allocated i-number, whatever is in it.  s_tinode is derived from this, so a file
    // of a type this program does not recognise still has to be counted.
    nfiles++;

    if (mode == S_IFCHR) {
        ncfile++;
        return;
    }
    if (mode == S_IFBLK) {
        nbfile++;
        return;
    }
    if (mode == S_IFDIR)
        ndfile++;
    else if (mode == S_IFREG)
        nrfile++;
    else {
        printf("bad mode %d\n", inum);
        return;
    }

    // v7's l3tol(): the addresses were three packed bytes each there and are whole words
    // here (sys/ino.h), so this is a copy -- and it stays a copy rather than a walk of
    // ip->di_addr, because ckindir() reads into inoslot's neighbours and a later inode
    // block read would move what ip points at.
    for (i = 0; i < NADDR; i++)
        iaddr[i] = ip->di_addr[i];

    for (i = 0; i < NADDR - NLEVEL; i++) {
        if (iaddr[i] == 0)
            continue;
        ndirect++;
        chk(iaddr[i], "data (small)");
    }
    for (i = 0; i < NLEVEL; i++) {
        if (iaddr[NADDR - NLEVEL + i] == 0)
            continue;
        ckindir(iaddr[NADDR - NLEVEL + i], i + 1, 1);
    }
}

//
// One indirect block.  `levels' is how many levels of indirection remain below it -- 1 when
// its entries are data blocks -- and `depth' how far it is from the inode, which is what
// names it in a diagnostic.  The buffer is indexed by `levels', so each recursion has its
// own and nothing is re-read; see the head comment on why this is not fsck's idiom.
//
static void ckindir(daddr_t blk, int levels, int depth)
{
    daddr_t *ap;
    int i;

    if (depth == 1)
        nindir++;
    else
        niindir++;
    if (chk(blk, indclass[depth - 1]))
        return;

    bread(indslot[levels - 1], blk);
    ap = (daddr_t *)indslot[levels - 1];
    for (i = 0; i < NINDIR; i++) {
        if (ap[i] == 0)
            continue;
        if (levels == 1) {
            ndirect++;
            chk(ap[i], datclass[depth - 1]);
        } else
            ckindir(ap[i], levels - 1, depth + 1);
    }
}

//
// Claim one block for the inode being walked.  Returns 1 when the number is unusable, so
// the caller does not go on to read it.
//
static int chk(daddr_t bno, const char *s)
{
    int n;

    // Every number printed here is a block NUMBER: an on-disk quantity, not a measurement.
    // ../README.md SS4, and fsck.c makes the same note at the same place.
    if (bno < fmin || bno >= fmax) {
        printf("%d bad; inode=%d, class=%s\n", bno, inum, s);
        return 1;
    }
    if (duped(bno)) {
        printf("%d dup; inode=%d, class=%s\n", bno, inum, s);
        ndup++;
    }
    for (n = 0; blist[n] != -1; n++)
        if (bno == blist[n])
            printf("%d arg; inode=%d, class=%s\n", bno, inum, s);
    return 0;
}

// Test and set one bit of the block map.  A `char' is an eight-bit byte here, six to a
// word, so v7's bit arithmetic carries unchanged.
static int duped(daddr_t bno)
{
    daddr_t d;
    int m, n;

    if (dflg)
        return 0;
    d = bno - fmin;
    m = 1 << (d % BITSPB);
    n = d / BITSPB;
    if (dupmap[n] & m)
        return 1;
    dupmap[n] |= m;
    return 0;
}

//
// Pop one block off the free list, chaining through a struct fblk when the superblock's own
// cache empties -- the walk alloc() (kernel/alloc.c) performs, so a list this accepts is
// one the kernel can use.  df.c's alloc() is the same walk.
//
static daddr_t alloc(void)
{
    struct fblk *fb;
    daddr_t bno;
    int i;

    if (sblock.s_nfree <= 0)
        return 0;
    if (sblock.s_nfree > NICFREE) {
        printf("Bad free list, s.b. count = %d\n", sblock.s_nfree);
        return 0;
    }
    bno                           = sblock.s_free[--sblock.s_nfree];
    sblock.s_free[sblock.s_nfree] = 0;
    if (bno == 0)
        return bno;
    if (sblock.s_nfree <= 0) {
        if (bno < fmin || bno >= fmax) {
            printf("Bad free list, chain block %d\n", bno);
            return 0;
        }
        bread(fbslot, bno);
        fb             = (struct fblk *)fbslot;
        sblock.s_nfree = fb->df_nfree;
        if (sblock.s_nfree < 0 || sblock.s_nfree > NICFREE) {
            printf("Bad free list, entry count of block %d = %d\n", bno, sblock.s_nfree);
            sblock.s_nfree = 0;
            return 0;
        }
        for (i = 0; i < NICFREE; i++)
            sblock.s_free[i] = fb->df_free[i];
    }
    return bno;
}

//
// -s.  Lay the free list down again from the block map, then write the superblock.  This is
// fsck.c's phase 6 rather than v7's: there is no s_m/s_n interleave to reproduce, so the
// list descends plainly and a salvaged volume comes out the shape ../mkfs/mkfs.c makes one.
//
static void makefree(void)
{
    struct fblk *fb;
    daddr_t blk;
    int i;

    fb = (struct fblk *)fbslot;

    sblock.s_nfree  = 0;
    sblock.s_ninode = 0;
    sblock.s_flock  = 0;
    sblock.s_ilock  = 0;
    sblock.s_fmod   = 0;
    sblock.s_ronly  = 0;
    sblock.s_tfree  = 0;

    // NOT v7's zero.  Nothing in v7 maintained this field, so writing it away cost nothing
    // there; kernel/alloc.c maintains it here and cmd/fsutil/check.cpp faults an image on
    // it, so a volume salvaged v7's way would come back broken to the host.  The `- 1' is
    // inode 1: it exists and ialloc() can never hand it out, so it is neither in use nor
    // free.  fsck.c derives it the same way.
    sblock.s_tinode = imax - nfiles - 1;
    sblock.s_time   = time((time_t *)0);

    clearwords(fbslot, BSIZEW);
    fb->df_nfree = 1; // slot 0 is the link to the next chain block, and is zero: end of list

    for (blk = fmax - 1; blk >= fmin; blk--) {
        if (duped(blk))
            continue;
        sblock.s_tfree++;
        if (fb->df_nfree >= NICFREE) {
            // The block being freed becomes the chain block holding the previous NICFREE
            // entries, and then slot 0 of the next chain points back at it.
            bwrite(fbslot, blk);
            clearwords(fbslot, BSIZEW);
        }
        fb->df_free[fb->df_nfree] = blk;
        fb->df_nfree++;
    }
    sblock.s_nfree = fb->df_nfree;
    for (i = 0; i < NICFREE; i++)
        sblock.s_free[i] = fb->df_free[i];

    // The superblock LAST, so a run that dies partway leaves a volume whose superblock
    // still describes the old list rather than half of a new one.  ../mkfs/README.md's
    // commit-last rule.
    bwrite(sbslot, SUPERB);
    sync();
}

//
// One block, in and out.  Both obey the four conditions ../df/README.md lists: a whole
// BSIZE into an MDALIGN-aligned buffer at a block-aligned offset.  The multiply is a
// multiply -- v7's `bno*BSIZE' happened to be one already, there being no BSHIFT here.
//
static void bread(int *buf, daddr_t bno)
{
    if (lseek(fi, (off_t)bno * BSIZE, SEEK_SET) >= 0 && read(fi, (char *)buf, BSIZE) == BSIZE)
        return;
    printf("read error %d\n", bno);
    if (sflg) {
        printf("No update\n");
        sflg = 0;
    }
    clearwords(buf, BSIZEW);
}

static void bwrite(int *buf, daddr_t bno)
{
    if (lseek(fi, (off_t)bno * BSIZE, SEEK_SET) >= 0 && write(fi, (char *)buf, BSIZE) == BSIZE) {
        modified = 1;
        return;
    }
    printf("write error %d\n", bno);
    nerror |= 04;
}

static void clearwords(int *p, int n)
{
    while (n-- > 0)
        *p++ = 0;
}

//
// Is `dev' the raw name of the device the root is mounted on?  4.xBSD's unrawname(): drop
// the `r' from the last path component and ask about that name instead.  Copied from
// fsck.c, which is where the reasoning is; every comparison is by index, a path being
// walked and never ordered (../README.md SS2).
//
static int israwroot(const char *dev, dev_t rootdev)
{
    char name[64];
    struct stat st;
    int i, n, last;

    for (n = 0; dev[n] != 0; n++) {
        if (n >= (int)sizeof(name) - 1)
            return 0;
        name[n] = dev[n];
    }
    name[n] = 0;

    last = 0;
    for (i = 0; i < n; i++)
        if (name[i] == '/')
            last = i + 1;
    if (name[last] != 'r')
        return 0;
    for (i = last; i < n; i++)
        name[i] = name[i + 1];

    if (stat(name, &st) < 0)
        return 0;
    return (st.st_mode & S_IFMT) == S_IFBLK && st.st_rdev == rootdev;
}
