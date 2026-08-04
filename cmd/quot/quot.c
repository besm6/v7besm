/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// quot -- disk usage by user.
//
// The v7 program, unchanged in what it does: open the raw device, sync, read the
// superblock, and sweep the whole i-list adding each inode's block count to its owner's
// total.  It never learns a file's NAME -- only its inode's uid -- which is what makes it
// different from du(1) beside it and why `-n' exists at all: that mode correlates the
// running i-number against an ncheck(1M) listing on standard input.  /etc/ncheck arrived
// with task C4e and is on the image; sort(1) is task C5d and is not, so the pipeline
// quot.1m gives has its producer but not yet its middle.
//
// A BLOCK IS 1024 BYTES IN WHAT THIS PRINTS, and 3072 in what it counts.  The filesystem's
// block is BSIZE == 3072; this reports KBPB == 3 of them per block (sys/param.h), so that a
// number means something without knowing BSIZE.  Two consequences worth expecting: every
// count is a MULTIPLE OF 3, the smallest thing that can be allocated being one 3072-byte
// block; and a number is half a PDP-11's rather than a sixth, v7's block having been 512
// bytes.  THE MULTIPLY IS AT THE printf and nowhere else -- every count in this file is a
// filesystem block until it is printed.  quot.1m says so; ../README.md SS4 is the rule.
//
// Holes are still counted, as v7's BUGS section says: di_size is what this divides, so a
// file's indirect blocks are not counted and its holes are.  icheck(1) is the program that
// asks the other question.
//
// THE READ IS THE INTERESTING PART, and it is df(1)'s: a raw transfer through /dev/rmd0
// goes physio() -> mdstrategy(), and those two impose four rules v7 knows nothing about --
// byte #0 of a word, a whole number of BSIZEs, an MDALIGN-aligned buffer, and a
// block-aligned seek.  ../df/README.md is the account and is the thing to read before
// touching bread() below.  What it costs here is one aligned block buffer; what it BUYS is
// that v7's `struct dinode itab[256]' disappears, because one block IS INOPB inodes and the
// buffer is the i-node table.  That is 4,096 words of bss saved, not spent.
//
// WHAT THE PORT HAD TO CHANGE, beyond the mechanical C11 pass:
//
//  1. `#include <sys/inode.h>' -- THE KERNEL'S in-core inode header, which declares
//     `extern struct inode inode[]' and has no business in a user program.  v7 included it
//     for IFMT/IFDIR/IFREG alone; <sys/stat.h>'s S_IFMT/S_IFDIR/S_IFREG are the same three
//     values and are the user-side spelling.
//
//  2. FOUR `%D's and a `%u'.  %D is not a conversion here: doprnt.c echoes it verbatim AND
//     consumes no argument, so `printf("%d\t%d\t%D\n", i, sizes[i], t)' would have printed
//     the two characters `%D' and left t on the stack.  All five are `%d'.
//
//  3. Every `unsigned' is an int.  An unsigned add, compare or divide on this machine is a
//     CALL -- b$uadd, b$ult, b$udiv -- because the additive unit reads the top bits as an
//     exponent; sys/param.h says so at length.  Nothing here needs more than 41 bits.
//
//  4. `long' is one word, so blocks, nfiles, overflow and report()'s t are int.
//
//  5. qcmp() takes two const void * -- <stdlib.h>'s qsort(3) prototype -- and casts inside,
//     as ls(1)'s compar() does.  A struct du is four words, which is the case qsort
//     exchanges a word at a time.
//
// THREE UPSTREAM BUGS, fixed rather than carried:
//
//   * `if (n > NUID) continue;' let a /etc/passwd entry with uid exactly NUID write
//     du[NUID].name -- one element past a NUID-element array.  It is `>='.
//
//   * qcmp() called strcmp(p1->name, p2->name) unguarded, and qsort sorts ALL NUID entries,
//     of which almost all have name == NULL.  On a PDP-11 address 0 was readable and this
//     was silently harmless; here it dereferences word 0.  Both sides are guarded, and a
//     named user sorts ahead of a bare number so the order is still total.
//
//   * isdigit(c = getchar()) with c possibly EOF.  <ctype.h>'s tables have 256 entries here
//     (../sh/README.md's task-C11 section on why), and EOF is -1, which is in front of all
//     of them.
//
// AND ONE THING LEFT AS v7 WROTE IT, deliberately: the accumulators are not cleared between
// filesystems, so `quot a b' reports running totals.  There is one filesystem on this
// machine (../../root.manifest: one EC-5052, no partitions), so the case cannot arise, and
// inventing a divergence for it would be a change nothing can test.
//
// NOT SETUID.  /dev/rmd0 is mode 0600 because that one node is every file's contents;
// quot is a root-only program here and quot.1m says so.  ../README.md SS8 is the rule.
//

// The order here does not matter and cannot be made to: clang-format sorts a block of <>
// includes alphabetically, so the on-disk-layout headers arrive ahead of the sys/param.h
// and sys/types.h they need.  They include what they depend on themselves -- see the note
// at the head of each, and sys/dir.h, which is the precedent.
#include <ctype.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/filsys.h>
#include <sys/ino.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// What this file assumes of the layout, asserted rather than re-derived (../TODO.md, task
// C4).  sys/ino.h carries the rest -- that a dinode is sixteen words and that INOPB of them
// tile a block -- which is exactly what lets the block buffer BE the i-node table.
_Static_assert(BSIZE == BSIZEW * NBPW, "a block must be BSIZEW words of NBPW bytes");
_Static_assert(INOPB * sizeof(struct dinode) == BSIZE, "INOPB dinodes must tile a block");
_Static_assert(BSIZE % KBYTE == 0, "a block must be a whole number of reported blocks");

// mdstrategy()'s half-zone, which is also a block: kernel/dev/md.c refuses a transfer whose
// physical address is not a multiple of it.  A page is PGSZ words and mapping preserves the
// offset within a page, so aligning the virtual address aligns the physical one.
#define MDALIGN BSIZEW
_Static_assert(PGSZ % MDALIGN == 0, "a page must be a whole number of MDALIGNs");

#define NUID  300 // uids reported on; above this an inode is not counted
#define TSIZE 500 // -c histogram buckets, the last one cumulative

static char *dargv[] = { "/dev/rmd0", 0 };

static struct filsys sblock;

// The one read target: BSIZEW words of it are used and the rest is the slack the alignment
// step needs, since where bss lands is the linker's business.  It doubles as the i-node
// table, one block of INOPB dinodes at a time.
static int rawbuf[BSIZEW + MDALIGN];
static int *blk;

static struct du {
    int blocks;
    int nfiles;
    int uid;
    char *name;
} du[NUID];

static int sizes[TSIZE];
static int overflow;

static int nflg;
static int fflg;
static int cflg;

static int fi;
static int ino;
static int nfiles;

static void check(char *file);
static void acct(struct dinode *ip);
static void bread(daddr_t bno);
static void report(void);
static char *copy(const char *s);

int main(int argc, char **argv)
{
    int n;
    struct passwd *lp;
    char **p;

    // An `int *' IS a word address on this machine -- lib/test/memt.c casts one the same
    // way -- so the alignment is ordinary arithmetic.
    blk = rawbuf;
    while ((int)blk % MDALIGN != 0)
        blk++;

    for (n = 0; n < NUID; n++)
        du[n].uid = n;
    while ((lp = getpwent()) != 0) {
        n = lp->pw_uid;
        if (n < 0 || n >= NUID) // v7 wrote `n > NUID', one past the end
            continue;
        if (du[n].name)
            continue;
        du[n].name = copy(lp->pw_name);
    }
    if (argc == 1) {
        for (p = dargv; *p;) {
            check(*p++);
            report();
        }
        return 0;
    }
    while (--argc) {
        argv++;
        if (argv[0][0] == '-') {
            if (argv[0][1] == 'n')
                nflg++;
            else if (argv[0][1] == 'f')
                fflg++;
            else if (argv[0][1] == 'c')
                cflg++;
        } else {
            check(*argv);
            report();
        }
    }
    return 0;
}

//
// Sweep one filesystem's i-list.
//
static void check(char *file)
{
    int i, j, c;
    struct dinode *itab;

    fi = open(file, O_RDONLY);
    if (fi < 0) {
        printf("cannot open %s\n", file);
        return;
    }
    printf("%s:\n", file);

    // Load-bearing on the raw path: this read bypasses the buffer cache that the mounted
    // filesystem is still writing through.  See ../df/README.md.
    sync();

    // The superblock is exactly one block, so this is a whole-block read by construction.
    // It is copied out of the shared buffer because the i-list sweep below reads into it.
    bread(SUPERB);
    for (i = 0; i < BSIZEW; i++)
        ((int *)&sblock)[i] = blk[i];

    // s_isize is the FIRST DATA BLOCK, not a count -- so the i-list is blocks SUPERB+1
    // through s_isize-1, and itod() puts inode 1 at the start of SUPERB+1.
    nfiles = (sblock.s_isize - (SUPERB + 1)) * INOPB;
    ino    = 0;
    if (nflg) {
        c = getchar();
        if (c != EOF && isdigit(c))
            ungetc(c, stdin);
        else
            while (c != '\n' && c != EOF)
                c = getchar();
    }

    // One block per pass, because one block IS the i-node table: rule 2 of the four in
    // ../df/README.md permits no other shape anyway.
    for (i = SUPERB + 1; ino < nfiles; i++) {
        bread((daddr_t)i);
        itab = (struct dinode *)blk;
        for (j = 0; j < INOPB && ino < nfiles; j++) {
            ino++;
            acct(&itab[j]);
        }
    }
}

//
// Account for one inode.
//
static void acct(struct dinode *ip)
{
    int n;
    char *np;
    static int fino;

    if ((ip->di_mode & S_IFMT) == 0)
        return;
    if (cflg) {
        if ((ip->di_mode & S_IFMT) != S_IFDIR && (ip->di_mode & S_IFMT) != S_IFREG)
            return;
        n = (ip->di_size + BSIZE - 1) / BSIZE;
        if (n >= TSIZE) {
            overflow += n;
            n = TSIZE - 1;
        }
        sizes[n]++;
        return;
    }
    // A negative test as well as v7's upper one: di_uid is a signed 41-bit word here, not
    // v7's sixteen unsigned bits, so a garbage inode can index backwards.
    if (ip->di_uid < 0 || ip->di_uid >= NUID)
        return;
    du[ip->di_uid].blocks += (ip->di_size + BSIZE - 1) / BSIZE;
    du[ip->di_uid].nfiles++;
    if (nflg) {
    tryagain:
        if (fino == 0)
            if (scanf("%d", &fino) <= 0)
                return;
        if (fino > ino)
            return;
        if (fino < ino) {
            while ((n = getchar()) != '\n' && n != EOF)
                ;
            fino = 0;
            goto tryagain;
        }
        if ((np = du[ip->di_uid].name) != NULL)
            printf("%.7s\t", np);
        else
            printf("%d\t", ip->di_uid);
        while ((n = getchar()) == ' ' || n == '\t')
            ;
        putchar(n);
        while (n != EOF && n != '\n') {
            n = getchar();
            putchar(n);
        }
        fino = 0;
    }
}

//
// One block, into the one aligned buffer.  Whole blocks at block-aligned offsets are the
// only shape a raw transfer here can take; ../df/README.md is the account.
//
static void bread(daddr_t bno)
{
    lseek(fi, (off_t)bno * BSIZE, SEEK_SET);
    if (read(fi, (char *)blk, BSIZE) != BSIZE) {
        printf("read error %d\n", bno);
        exit(1);
    }
}

//
// Most blocks first, and a name breaks a tie.  v7 called strcmp() on two pointers that are
// NULL for every uid /etc/passwd does not mention -- which is most of the NUID entries
// qsort() is handed.
//
static int qcmp(const void *a, const void *b)
{
    const struct du *p1 = a;
    const struct du *p2 = b;

    if (p1->blocks > p2->blocks)
        return -1;
    if (p1->blocks < p2->blocks)
        return 1;
    if (p1->name && p2->name)
        return strcmp(p1->name, p2->name);
    if (p1->name)
        return -1; // a named user ahead of a bare number, so the order is total
    if (p2->name)
        return 1;
    return p1->uid - p2->uid;
}

static void report(void)
{
    int i;

    if (nflg)
        return;
    if (cflg) {
        int t = 0;
        //
        // THE BUCKET INDEX STAYS A FILESYSTEM BLOCK COUNT and only the printed columns
        // are multiplied, which is the whole reason this program converts at the printf
        // rather than at the source.  Index the histogram in reported blocks instead and
        // TSIZE covers a third of the file sizes it used to, while two buckets in three
        // can never be occupied -- a file's size in KiB is always a multiple of KBPB.
        //
        // Columns one and three are block counts; column two is a count of FILES and is
        // not touched.
        //
        for (i = 0; i < TSIZE - 1; i++)
            if (sizes[i]) {
                t += i * sizes[i];
                printf("%d\t%d\t%d\n", i * KBPB, sizes[i], t * KBPB);
            }
        printf("%d\t%d\t%d\n", (TSIZE - 1) * KBPB, sizes[TSIZE - 1], (overflow + t) * KBPB);
        return;
    }
    qsort(du, NUID, sizeof(du[0]), qcmp);
    for (i = 0; i < NUID; i++) {
        if (du[i].blocks == 0)
            return;
        printf("%5d\t", du[i].blocks * KBPB);
        if (fflg)
            printf("%5d\t", du[i].nfiles);
        if (du[i].name)
            printf("%s\n", du[i].name);
        else
            printf("#%d\n", du[i].uid);
    }
}

//
// strdup(3) by hand, as v7 wrote it, and the reason it is wanted at all: <pwd.h> hands
// back pointers into one shared line buffer, and the next getpwent() overwrites it.
// This libc has grown a strdup() since (task C9e, cmd/cc wanted it nine times over), so
// this is now a transcription rather than a necessity; it is kept because the NULL
// branch below is this program's own decision and not the library's.
//
static char *copy(const char *s)
{
    char *p;
    int n;

    for (n = 0; s[n]; n++)
        ;
    p = malloc((size_t)n + 1);
    if (p == NULL)
        return NULL; // the uid prints as `#nnn'; v7 would have stored the NULL anyway
    for (n = 0; (p[n] = s[n]) != '\0'; n++)
        ;
    return p;
}
