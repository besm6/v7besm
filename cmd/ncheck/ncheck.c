/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// ncheck -- generate names from i-numbers.
//
//	/etc/ncheck [ -i numbers ] [ -a ] [ -s ] [ filesystem ]
//
// Task C4e, and the one of the four that is useful on its own: nothing else on this image
// can put a NAME to an i-number.  Every other program here works the other way round --
// namei() walks a path to an inode -- and the two programs that report an i-number,
// fsck(1M) and quot(1M), can only print the number.  quot -n has been waiting for this one
// since task C4a and says so; `ncheck | sort | quot -n' is what it wants.
//
// Three sweeps of the i-list.  Pass 1 enters every directory in a table; pass 2 gives each
// of those its parent and its name; pass 3 walks the directories again and prints each
// entry, building the path by following parents back to the root.
//
// THE READ PATH IS df(1)'s, and ../df/README.md is the thing to read before touching
// bread() below.  As in ../dcheck and ../icheck, v7's `NI 16' i-list cache is gone -- a raw
// transfer is ONE block -- and its `struct direct dbuf[NDIR]' and `daddr_t ibuf[NINDIR]'
// automatics, 512 words each and TWO of the first (pass 2 and pass 3 both carry one), are
// slots of one aligned bss array now.
//
// v7's bmap() IS WRONG HERE THREE WAYS, exactly as ../dcheck/dcheck.c's head comment sets
// out: `iaddr[NADDR-3]' is a direct block at NADDR 8, `i > NINDIR' is off by one, and there
// is no double-indirect arm.  dirblock() below is the same two-level replacement.
//
// THE HASH TABLE IS GONE, and that is the one structural decision in this port.  v7 has a
// fixed `struct htab htab[HSIZE]' with HSIZE 2503, open-addressed, dying with "out of
// core-- increase HSIZE" when it fills.  An entry is five words here (two i-numbers and
// DIRSIZ bytes, six chars to a word), so that table is 12,515 words -- 44% of the 28,672 a
// program has (../README.md SS6) -- and it was sized against a PDP-11's i-list.  This
// machine's whole drive is 2000 blocks and mkfs's default i-list is one inode per two
// blocks, so imax can never exceed about 1,024 and three fifths of that table is
// unreachable BY CONSTRUCTION.  So the table is calloc'd at imax+1 entries and indexed
// DIRECTLY by i-number: no hash, no probe, no collision, no fixed ceiling, and a refusal at
// startup rather than an exit partway through if the i-list really is too large.
//
// THREE UPSTREAM BUGS, fixed rather than carried:
//
//   * `nxfile' is the last index used rather than a count, so `-i 4 5 6' leaves nxfile at 2
//     and the first inode -s appends overwrites i-number 6.  It is a count here.
//   * the -i loop can leave i == NB, and `ilist[nxfile+1] = 0' can then write one past a
//     NB-element array.  The array is NB+1 long and both are bounded.
//   * pass1's -s arm is missing its braces: the `return' is indented as though it belonged
//     to the `if' and does not.  It happens to be correct; it is written correctly now.
//
// NOTHING IT PRINTS IS A BLOCK, so ../README.md SS4's KBPB conversion does not reach this
// program: it prints i-numbers and path names.  A name is printed `%.*s' with DIRSIZ, which
// is 18 here rather than v7's 14, and a name read out of a directory is not NUL-terminated
// (SS5).  Names carry eight bits and are not masked (SS11).
//
// NOT SETUID: it reads /dev/rmd0, which is mode 0600 because that one node is every file's
// contents -- and a program that prints every path name on the volume is a program that
// reports on directories the caller may not be able to read.  ../README.md SS8.
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

#define NB     100 // -i i-numbers remembered, and the -s cap
#define MAXLEV 10  // pname() gives up this deep; a `...' says so

// The block buffers, cut from one aligned array: the superblock, a block of the i-list, a
// directory block and NLEVEL indirect blocks.
#define NSLOTS (3 + NLEVEL)

static int rawbuf[NSLOTS * BSIZEW + MDALIGN];
static int *sbslot;
static int *inoslot;
static int *dirslot;
static int *indslot[NLEVEL];

// The superblock lives IN an aligned slot rather than in a struct of its own; ../icheck,
// ../dcheck and fsck.c do the same.
#define sblock (*(struct filsys *)sbslot)

// One directory, remembered.  h_ino doubles as the occupancy marker now that the table is
// indexed by i-number: tab[i].h_ino == i means i is a known directory.
struct htab {
    ino_t h_ino;
    ino_t h_pino;
    char h_name[DIRSIZ];
};

static char *dargv[] = { "/dev/rmd0", 0 };

static struct htab *tab;
static ino_t ilist[NB + 1];
static int nxfile; // entries of ilist[] in use -- a COUNT; v7 kept the last index

static int aflg;
static int sflg;
static int fi;
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
static void pass3(struct dinode *ip);
static int dotname(struct direct *dp);
static void pname(ino_t i, int lev);
static struct htab *lookup(ino_t i, int ef);
static daddr_t dirblock(int lbn);
static void bread(int *buf, daddr_t bno);
static void clearwords(int *p, int n);

int main(int argc, char **argv)
{
    int *p;
    int i, n, nchecked;

    // An `int *' IS a word address on this machine, so the alignment is ordinary
    // arithmetic; df.c, quot.c, mkfs.c, fsck.c, ../icheck and ../dcheck all step this way.
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
            case 'a':
                aflg++;
                continue;
            case 'i':
                for (i = nxfile; i < NB && argc > 1; i++) {
                    n = atoi(argv[1]);
                    if (n == 0)
                        break;
                    ilist[i] = n;
                    nxfile   = i + 1; // a COUNT; v7 wrote `nxfile = i'
                    argv++;
                    argc--;
                }
                continue;
            case 's':
                sflg++;
                continue;
            default:
                // v7 fell through into check() here and then complained that it could not
                // open the flag.
                fprintf(stderr, "ncheck: bad flag %c\n", (*argv)[1]);
                nerror++;
                continue;
            }
        }
        check(*argv);
        nchecked++;
    }

    // v7's DESCRIPTION promises "a set of default file systems"; there is one drive here
    // and no partitions, so the set is one entry -- quot.c's dargv[].  (df's is gone: it
    // takes its default from the kernel's own mount table now.)
    if (nchecked == 0)
        check(dargv[0]);
    return nerror;
}

//
// One filesystem: three sweeps of the i-list.
//
static void check(char *file)
{
    struct dinode *itab;
    daddr_t iblk;
    int pass;

    if (setup(file) == NO)
        return;

    itab = (struct dinode *)inoslot;
    for (pass = 1; pass <= 3; pass++) {
        iblk = -1;
        for (inum = 1; inum <= imax; inum++) {
            // The i-list is re-read on every sweep: pass 1's -s arm aside, each pass reads
            // directory and indirect blocks through the other slots, but a later sweep
            // starts from the top and the cached block is the wrong one.
            if (itod(inum) != iblk) {
                iblk = itod(inum);
                bread(inoslot, iblk);
            }
            if (pass == 1)
                pass1(&itab[itoo(inum)]);
            else if (pass == 2)
                pass2(&itab[itoo(inum)]);
            else
                pass3(&itab[itoo(inum)]);
        }
        // The -s arm of pass 1 appends to ilist, so the terminator goes down between the
        // first sweep and the filtering the third one does.
        if (pass == 1)
            ilist[nxfile] = 0;
    }

    close(fi);
    free(tab);
    tab = NULL;
}

//
// Open the device, read its superblock and size the directory table from it.
//
static int setup(char *file)
{
    fi = open(file, O_RDONLY);
    if (fi < 0) {
        fprintf(stderr, "ncheck: cannot open %s\n", file);
        nerror++;
        return NO;
    }
    printf("%s:\n", file);

    // Load-bearing on the raw path: this read bypasses the buffer cache the mounted
    // filesystem is still writing through.  ../df/README.md.
    sync();

    bread(sbslot, SUPERB);

    // THE GEOMETRY WORDS FIRST, which v7 has none of to look at.  ../fsck/README.md SS1.
    if (sblock.s_magic != FS_MAGIC) {
        fprintf(stderr, "ncheck: %s: not a filesystem\n", file);
        close(fi);
        nerror++;
        return NO;
    }
    if (sblock.s_bsize != BSIZEW || sblock.s_inopb != INOPB || sblock.s_naddr != NADDR) {
        fprintf(stderr, "ncheck: %s: filesystem geometry mismatch\n", file);
        fprintf(stderr, "bsize %d inopb %d naddr %d; this system wants %d %d %d\n", sblock.s_bsize,
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
        fprintf(stderr, "ncheck: check fsize and isize: %d, %d\n", sblock.s_fsize, sblock.s_isize);
        close(fi);
        nerror++;
        return NO;
    }

    // Sized from the superblock, not from a 1979 guess; see the head comment.
    tab = calloc((unsigned)(imax + 1), sizeof(*tab));
    if (tab == NULL) {
        fprintf(stderr, "ncheck: can't get memory for %d inodes\n", imax);
        close(fi);
        nerror++;
        return NO;
    }
    return YES;
}

//
// Pass 1: remember every directory.  Under -s, also collect the special files and the
// setuid ones, which is what makes `ncheck -s' a security report.
//
static void pass1(struct dinode *ip)
{
    int mode;

    mode = ip->di_mode & S_IFMT;
    if (mode != S_IFDIR) {
        // v7's braces are missing here; the `return' is outside the `if' and is meant to
        // be.  Written out rather than left as a trap for the next reader.
        if (sflg == 0 || nxfile >= NB)
            return;
        if (mode == S_IFBLK || mode == S_IFCHR || (ip->di_mode & (S_ISUID | S_ISGID)))
            ilist[nxfile++] = inum;
        return;
    }
    lookup(inum, 1);
}

//
// Pass 2: give every remembered directory its parent and its name.
//
static void pass2(struct dinode *ip)
{
    struct direct *dbuf;
    struct htab *hp;
    off_t doff, size;
    daddr_t d;
    ino_t kno;
    int i, j, k;

    if ((ip->di_mode & S_IFMT) != S_IFDIR)
        return;

    // v7's l3tol(): whole words here (sys/ino.h), so a copy -- into the file-scope array
    // dirblock() reads.
    for (i = 0; i < NADDR; i++)
        iaddr[i] = ip->di_addr[i];
    size = ip->di_size;

    doff = 0;
    for (i = 0; doff < size; i++) {
        d = dirblock(i);
        if (d == 0)
            break;
        if (d < fmin || d >= fmax)
            break;
        bread(dirslot, d);
        dbuf = (struct direct *)dirslot;
        for (j = 0; j < DIRPB && doff < size; j++) {
            doff += sizeof(struct direct);
            kno = dbuf[j].d_ino;
            if (kno == 0)
                continue;
            hp = lookup(kno, 0);
            if (hp == 0)
                continue;
            if (dotname(&dbuf[j]))
                continue;
            hp->h_pino = inum;
            for (k = 0; k < DIRSIZ; k++)
                hp->h_name[k] = dbuf[j].d_name[k];
        }
    }
}

//
// Pass 3: print the selected entries, each with the path of the directory it is in.
//
static void pass3(struct dinode *ip)
{
    struct direct *dbuf;
    off_t doff, size;
    daddr_t d;
    ino_t kno;
    int i, j, k, want;

    if ((ip->di_mode & S_IFMT) != S_IFDIR)
        return;

    for (i = 0; i < NADDR; i++)
        iaddr[i] = ip->di_addr[i];
    size = ip->di_size;

    doff = 0;
    for (i = 0; doff < size; i++) {
        d = dirblock(i);
        if (d == 0)
            break;
        if (d < fmin || d >= fmax)
            break;
        bread(dirslot, d);
        dbuf = (struct direct *)dirslot;
        for (j = 0; j < DIRPB && doff < size; j++) {
            doff += sizeof(struct direct);
            kno = dbuf[j].d_ino;
            if (kno == 0)
                continue;
            if (aflg == 0 && dotname(&dbuf[j]))
                continue;
            want = (ilist[0] == 0);
            for (k = 0; ilist[k] != 0; k++)
                if (ilist[k] == kno)
                    want = 1;
            if (!want)
                continue;
            printf("%d\t", kno);
            pname(inum, 0);
            // %.*s, not v7's %.14s: DIRSIZ is 18 here and a name out of a directory is not
            // NUL-terminated.  ../README.md SS5.
            printf("/%.*s", DIRSIZ, dbuf[j].d_name);
            if (lookup(kno, 0))
                printf("/.");
            printf("\n");
        }
    }
}

static int dotname(struct direct *dp)
{
    if (dp->d_name[0] == '.') {
        if (dp->d_name[1] == 0)
            return 1;
        if (dp->d_name[1] == '.' && dp->d_name[2] == 0)
            return 1;
    }
    return 0;
}

//
// Print the path of directory i, root first.  The recursion is bounded at MAXLEV, as v7's
// is -- a `..' cycle on a damaged filesystem would otherwise run the 4,096-word stack out
// (../README.md SS6), and nothing checks that stack.
//
static void pname(ino_t i, int lev)
{
    struct htab *hp;

    if (i == ROOTINO)
        return;
    if ((hp = lookup(i, 0)) == 0) {
        printf("???");
        return;
    }
    if (lev > MAXLEV) {
        printf("...");
        return;
    }
    pname(hp->h_pino, lev + 1);
    printf("/%.*s", DIRSIZ, hp->h_name);
}

//
// Find, or with ef enter, the table slot for i-number i.  Indexed rather than hashed; see
// the head comment.  h_ino is the occupancy marker.
//
static struct htab *lookup(ino_t i, int ef)
{
    if (i < ROOTINO || i > imax)
        return 0;
    if (tab[i].h_ino == i)
        return &tab[i];
    if (ef == 0)
        return 0;
    tab[i].h_ino = i;
    return &tab[i];
}

//
// Map a directory's logical block number to a physical one.  ../dcheck/dcheck.c's head
// comment says what v7's bmap() got wrong here; this is the same two-level replacement.
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
    fprintf(stderr, "ncheck: %d - huge directory\n", inum);
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
    fprintf(stderr, "ncheck: read error %d\n", bno);
    clearwords(buf, BSIZEW);
}

static void clearwords(int *p, int n)
{
    while (n-- > 0)
        *p++ = 0;
}
