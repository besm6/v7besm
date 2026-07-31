/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// dcheck -- file system directory consistency check.
//
//	/etc/dcheck [ -i numbers ] [ filesystem ]
//
// Task C4e.  The second of fsck(1M)'s jobs done standalone: count the directory entries
// that name each i-number, and hold the total against that inode's di_nlink.  It is one of
// the five checks cmd/fsutil/check.cpp does on the host and the guest did not.
//
// THE READ PATH IS df(1)'s, and ../df/README.md is the thing to read before touching
// bread() below: a raw transfer through /dev/rmd0 goes physio() -> mdstrategy(), and those
// two want byte #0 of a word, a whole number of BSIZEs, a buffer whose WORD address is a
// multiple of MDALIGN, and a block-aligned seek.  So every buffer here is a slot of one
// aligned bss array, v7's `struct dinode itab[INOPB*NI]' i-list cache is gone -- a raw
// transfer is ONE block -- and its `struct direct dbuf[NDIR]' and `daddr_t ibuf[NINDIR]'
// automatics, 512 words apiece, are slots too.  ../icheck/icheck.c is the sibling that
// says the same at more length.
//
// v7's bmap() IS WRONG HERE THREE WAYS, and it is the only interesting part of the port:
//
//   * it names `iaddr[NADDR-3]' as the single indirect address.  NADDR is 13 in v7 and 8
//     here, so NADDR-3 is di_addr[5] -- a DIRECT block, which would have been read as a
//     block of addresses;
//   * `if (i > NINDIR)' is off by one: index NINDIR is one past a NINDIR-entry block;
//   * and there is no double-indirect arm at all, so a directory past the single indirect
//     silently returned garbage rather than an address.  NLEVEL is 2 here.
//
// dirblock() below is the two-level walk instead, ../icheck/icheck.c's shape.  A directory
// that big cannot occur in practice -- but "cannot occur" is exactly what `NADDR-3' was.
//
// ecount IS ONE WORD PER INODE, not v7's one byte.  v7 saturates the counter at 0377 and
// masks every read with &0377, because a PDP-11 filesystem could not spend more; di_nlink
// is a whole word here, so a link count above 255 is representable on the disk and the mask
// would have misreported it.  That deletes the saturation branch and three masks, and costs
// 1,024 words of heap on a full drive.  v7's "Only doing 40000 files" cap goes with it: the
// table is sized from the superblock and a failure is a refusal, which is fsck's call.
//
// NOTHING IT PRINTS IS A BLOCK, so ../README.md SS4's KBPB conversion does not reach this
// program at all: the two numbers in its report are a count of directory entries and a link
// count.  dcheck.1m therefore has no BLOCKS section, which is a difference from icheck.1m
// worth not `fixing'.
//
// NOT SETUID: it reads /dev/rmd0, which is mode 0600 because that one node is every file's
// contents.  ../README.md SS8.
//

// The order here does not matter and cannot be made to: clang-format sorts a block of <>
// includes alphabetically, so the on-disk-layout headers arrive ahead of the sys/param.h
// and sys/types.h they need.  They include what they depend on themselves.
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/dir.h>
#include <sys/filsys.h>
#include <sys/ino.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// What this file's arithmetic assumes of the layout, asserted rather than re-derived
// (../TODO.md, task C4).  The headers carry the rest.
_Static_assert(BSIZE == BSIZEW * NBPW, "a block must be BSIZEW words of NBPW bytes");
_Static_assert(INOPB * sizeof(struct dinode) == BSIZE, "INOPB dinodes must tile a block");
_Static_assert(DIRPB * sizeof(struct direct) == BSIZE, "DIRPB entries must tile a block");
_Static_assert(NINDIR * sizeof(daddr_t) == BSIZE, "NINDIR addresses must tile a block");
_Static_assert(NADDR > NLEVEL, "an inode must have at least one direct block");

// mdstrategy()'s half-zone, which is also a block: kernel/dev/md.c refuses a transfer whose
// physical address is not a multiple of it.  A page is PGSZ words and mapping preserves the
// offset within a page, so aligning the virtual address aligns the physical one.
#define MDALIGN BSIZEW
_Static_assert(PGSZ % MDALIGN == 0, "a page must be a whole number of MDALIGNs");

#define NO  0
#define YES 1

#define NB 10 // -i i-numbers remembered

// The block buffers, cut from one aligned array: the superblock, a block of the i-list, a
// directory block and NLEVEL indirect blocks.
#define NSLOTS (3 + NLEVEL)

static int rawbuf[NSLOTS * BSIZEW + MDALIGN];
static int *sbslot;
static int *inoslot;
static int *dirslot;
static int *indslot[NLEVEL];

// The superblock lives IN an aligned slot rather than in a struct of its own; ../icheck
// and fsck.c do the same, and there it is load-bearing because they write it back.
#define sblock (*(struct filsys *)sbslot)

static char *dargv[] = { "/dev/rmd0", 0 };

static ino_t ilist[NB + 1];
static int *ecount;

static int fi;
static int headpr;
static int nerror;

static ino_t inum; // the inode being walked
static ino_t imax; // i-numbers on the volume
static daddr_t fmin;
static daddr_t fmax;
static daddr_t iaddr[NADDR];

static void check(char *file);
static int setup(char *file);
static void pass1(struct dinode *ip);
static void pass2(struct dinode *ip);
static daddr_t dirblock(int lbn);
static void bread(int *buf, daddr_t bno);
static void clearwords(int *p, int n);

int main(int argc, char **argv)
{
    int *p;
    int i, n, nchecked;

    // An `int *' IS a word address on this machine, so the alignment is ordinary
    // arithmetic; df.c, quot.c, mkfs.c, fsck.c and ../icheck all step the same way.
    p = rawbuf;
    while ((int)p % MDALIGN != 0)
        p++;
    sbslot  = p;
    inoslot = p + BSIZEW;
    dirslot = p + 2 * BSIZEW;
    for (i = 0; i < NLEVEL; i++)
        indslot[i] = p + (3 + i) * BSIZEW;

    nchecked = 0;
    while (--argc > 0) {
        argv++;
        if (**argv == '-') {
            switch ((*argv)[1]) {
            case 'i':
                for (i = 0; i < NB && argc > 1; i++) {
                    n = atoi(argv[1]);
                    if (n == 0)
                        break;
                    ilist[i] = n;
                    argv++;
                    argc--;
                }
                ilist[i] = 0;
                continue;
            default:
                // v7 fell through into check() here and then complained that it could not
                // open the flag.
                printf("Bad flag %c\n", (*argv)[1]);
                nerror++;
                continue;
            }
        }
        check(*argv);
        nchecked++;
    }

    // v7's DESCRIPTION promises "a set of default file systems"; there is one drive here
    // and no partitions, so the set is one entry -- df.c's and quot.c's dargv[].
    if (nchecked == 0)
        check(dargv[0]);
    return nerror;
}

//
// One filesystem: sweep the i-list twice.  Pass 1 counts the directory entries that name
// each i-number; pass 2 holds those counts against the link counts.
//
static void check(char *file)
{
    struct dinode *itab;
    daddr_t iblk;

    if (setup(file) == NO)
        return;

    itab = (struct dinode *)inoslot;

    iblk = -1;
    for (inum = 1; inum <= imax; inum++) {
        if (itod(inum) != iblk) {
            iblk = itod(inum);
            bread(inoslot, iblk);
        }
        pass1(&itab[itoo(inum)]);
    }

    // The second sweep re-reads the i-list, pass 1 having read directory and indirect
    // blocks through the other slots.
    iblk = -1;
    for (inum = 1; inum <= imax; inum++) {
        if (itod(inum) != iblk) {
            iblk = itod(inum);
            bread(inoslot, iblk);
        }
        pass2(&itab[itoo(inum)]);
    }

    close(fi);
    free(ecount);
    ecount = NULL;
}

//
// Open the device, read its superblock and size the entry-count table from it.
//
static int setup(char *file)
{
    fi = open(file, O_RDONLY);
    if (fi < 0) {
        printf("cannot open %s\n", file);
        nerror++;
        return NO;
    }
    headpr = 0;
    printf("%s:\n", file);

    // Load-bearing on the raw path: this read bypasses the buffer cache the mounted
    // filesystem is still writing through.  ../df/README.md.
    sync();

    bread(sbslot, SUPERB);

    // THE GEOMETRY WORDS FIRST, which v7 has none of to look at.  sbcheck()
    // (kernel/alloc.c) refuses to mount a superblock that fails these, and so does every
    // other checker on this system; ../fsck/README.md SS1.
    if (sblock.s_magic != FS_MAGIC) {
        printf("%s: not a filesystem\n", file);
        close(fi);
        nerror++;
        return NO;
    }
    if (sblock.s_bsize != BSIZEW || sblock.s_inopb != INOPB || sblock.s_naddr != NADDR) {
        printf("%s: filesystem geometry mismatch\n", file);
        printf("bsize %d inopb %d naddr %d; this system wants %d %d %d\n", sblock.s_bsize,
               sblock.s_inopb, sblock.s_naddr, BSIZEW, INOPB, NADDR);
        close(fi);
        nerror++;
        return NO;
    }

    // s_isize is the FIRST DATA BLOCK, not a count, so the i-list is blocks SUPERB+1
    // through s_isize-1 and itod() puts inode 1 at the start of SUPERB+1.
    imax = ((ino_t)sblock.s_isize - (SUPERB + 1)) * INOPB;
    fmin = (daddr_t)sblock.s_isize;
    fmax = sblock.s_fsize;
    if (fmin >= fmax || imax <= 0) {
        printf("Check fsize and isize: %d, %d\n", sblock.s_fsize, sblock.s_isize);
        close(fi);
        nerror++;
        return NO;
    }

    ecount = calloc((unsigned)(imax + 1), sizeof(*ecount));
    if (ecount == NULL) {
        printf("Can't get memory\n");
        close(fi);
        nerror++;
        return NO;
    }
    return YES;
}

//
// One inode.  If it is a directory, charge every entry in it to the i-number it names.
//
static void pass1(struct dinode *ip)
{
    struct direct *dbuf;
    off_t doff, size;
    daddr_t d;
    ino_t kno;
    int i, j, k;

    if ((ip->di_mode & S_IFMT) != S_IFDIR)
        return;

    // v7's l3tol(): the addresses were three packed bytes each there and are whole words
    // here (sys/ino.h), so this is a copy -- into a file-scope array dirblock() reads.
    for (i = 0; i < NADDR; i++)
        iaddr[i] = ip->di_addr[i];
    size = ip->di_size;

    doff = 0;
    for (i = 0; doff < size; i++) {
        d = dirblock(i);
        if (d == 0)
            break;
        if (d < fmin || d >= fmax) {
            printf("%d bad dir blk; %d\n", d, inum);
            nerror++;
            break;
        }
        bread(dirslot, d);
        dbuf = (struct direct *)dirslot;
        for (j = 0; j < DIRPB && doff < size; j++) {
            doff += sizeof(struct direct);
            kno = dbuf[j].d_ino;
            if (kno == 0)
                continue;
            if (kno > imax || kno <= 1) {
                // %.*s, not v7's %.14s: DIRSIZ is 18 here, and a name out of a directory
                // is not NUL-terminated.  ../README.md SS5.
                printf("%5d bad; %d/%.*s\n", kno, inum, DIRSIZ, dbuf[j].d_name);
                nerror++;
                continue;
            }
            for (k = 0; ilist[k] != 0; k++)
                if (ilist[k] == kno) {
                    printf("%5d arg; %d/%.*s\n", kno, inum, DIRSIZ, dbuf[j].d_name);
                    nerror++;
                }
            ecount[kno]++;
        }
    }
}

//
// One inode, again: report it when the entries that name it and its link count disagree.
//
static void pass2(struct dinode *ip)
{
    if ((ip->di_mode & S_IFMT) == 0 && ecount[inum] == 0)
        return;
    if (ip->di_nlink == ecount[inum] && ip->di_nlink != 0)
        return;
    if (inum < ROOTINO && ip->di_nlink == 0 && ecount[inum] == 0)
        return;
    if (headpr == 0) {
        printf("     entries  link cnt\n");
        headpr++;
    }
    printf("%d\t%d\t%d\n", inum, ecount[inum], ip->di_nlink);
}

//
// Map a directory's logical block number to a physical one.  See the head comment for what
// v7's bmap() got wrong here; this is the general walk over NLEVEL levels.  The indirect
// chain is re-read on every call, as v7's is: a directory is a handful of blocks and the
// cache that would save is the one thing fsck had to think hard about.
//
static daddr_t dirblock(int lbn)
{
    daddr_t blk;
    int n, level, span, stride, i;

    if (lbn < NADDR - NLEVEL)
        return iaddr[lbn];

    n    = lbn - (NADDR - NLEVEL);
    span = NINDIR;
    for (level = 1; level <= NLEVEL; level++) {
        if (n < span) {
            blk = iaddr[NADDR - NLEVEL + level - 1];
            // NINDIR^(level-1), the stride of this level's index.  Not `div': <stdlib.h>
            // has that name, and b6lower rejects the shadowing outright.
            stride = 1;
            for (i = 1; i < level; i++)
                stride *= NINDIR;
            for (i = level; i > 0; i--) {
                if (blk < fmin || blk >= fmax)
                    return 0;
                bread(indslot[i - 1], blk);
                blk = ((daddr_t *)indslot[i - 1])[(n / stride) % NINDIR];
                stride /= NINDIR;
            }
            return blk;
        }
        n -= span;
        span *= NINDIR;
    }
    printf("%d - huge directory\n", inum);
    return 0;
}

//
// One block, into one aligned slot.  A whole BSIZE at a block-aligned offset is the only
// shape a raw transfer here can take; ../df/README.md.
//
static void bread(int *buf, daddr_t bno)
{
    if (lseek(fi, (off_t)bno * BSIZE, SEEK_SET) >= 0 && read(fi, (char *)buf, BSIZE) == BSIZE)
        return;
    // v7 wrote %D, which is not a conversion here: doprnt.c echoes it verbatim and
    // consumes no argument.
    printf("read error %d\n", bno);
    clearwords(buf, BSIZEW);
}

static void clearwords(int *p, int n)
{
    while (n-- > 0)
        *p++ = 0;
}
