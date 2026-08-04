/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 1999 Robert Nordier. All rights reserved. */
/* The report is 4.4BSD's df(1); see the note below. */

//
// df -- how much room a filesystem has left.
//
// THE REPORT IS BERKELEY'S AND THE NUMBERS ARE THE SUPERBLOCK'S.  What this prints is
// 4.4BSD's table -- Filesystem, 1K-blocks, Used, Avail, Capacity, Mounted on, and with -i
// the three i-node columns -- ported from RetroBSD's src/cmd/df.c.  Only the REPORT ported:
// that program is built on statfs(2) and getmntinfo(3), and this kernel had neither.  What
// it does have is the half of that source which predates both, its ufs_df(), which computes
// the same six numbers out of a superblock; that is what this file was.
//
// IT NOW HAS statfs(2), WRITTEN FOR THIS PROGRAM; getmntinfo(3) is still missing, which is
// why /etc/mtab and mtabread() stay.  So there are two routes and one rule:
//
//      A MOUNTED FILESYSTEM IS ASKED.  A DEVICE OR AN IMAGE IS READ.
//
// Asked is statfs(2): the four counts out of the IN-CORE superblock, no device opened and no
// privilege wanted, which is what makes a bare `df', `df /mnt' and `df /some/file' ordinary
// user commands.  Read is everything below -- and is also the STALER of the two, s_tfree
// living in core until update() writes it back, which is why it must sync(2) first and the
// asked route must not.  README.md is the account.
//
// WHERE EACH NUMBER COMES FROM, and the one place RetroBSD's arithmetic is wrong here:
//
//	1K-blocks	(s_fsize - s_isize) * KBPB	the DATA area: neither the superblock
//							nor the i-list is in any column
//	Avail		s_tfree * KBPB
//	Used		the difference
//	Capacity	used * 100 / (avail + used)
//	iused/ifree	(s_isize - (SUPERB+1)) * INOPB, and s_tinode
//
// RetroBSD's i-node count is `(fs_isize - 2) * INOPB', which skips a boot block THIS PORT
// DOES NOT HAVE: SUPERB is 0 here (sys/param.h) and the i-list starts at block 1.  Every
// sibling that sweeps the i-list spells it the way above -- icheck, dcheck, ncheck, clri,
// fsck, quot -- and on the root image, where s_isize is 33, the two differ by 32 inodes.
//
// s_tfree AND s_tinode ARE READ, which v7's df cannot do and this file could not until now.
// v7's own filsys(5) calls both fields uncurrent and its df walks the whole free list instead,
// popping it block by block through the chain blocks.  This kernel MAINTAINS them --
// alloc(), free(), ialloc() and ifree() in kernel/alloc.c, on RetroBSD's precedent; see
// sys/filsys.h -- so the answer is one lseek rather than a walk, and free INODES become
// askable at all, which no walk of the free list could have told us.
//
// THE WALK IS STILL HERE, BEHIND -w, and that is the point of keeping it.  Two independent
// readings of one number, on one disk, in one process: `df -w' must print exactly what `df'
// prints, and says so on stderr when it does not.  cmd/df/test/walk.expected is
// image.expected character for character, and kernel/test/fsinfo runs both against a THIRD
// reading -- the host's b6fsutil, which drains the free list the way the kernel's alloc()
// does.  -w and not -l: every other df spells -l "local filesystems only", and a letter that
// means something else here would be a trap for whoever knows one.
//
// A BLOCK IS 1024 BYTES IN WHAT THIS PRINTS, and 3072 in what it counts.  The filesystem's
// block is BSIZE == 3072; this reports KBPB == 3 of them per block (sys/param.h), so that a
// number means something without knowing BSIZE.  Two consequences worth expecting: every
// count is a MULTIPLE OF 3, the smallest thing that can be allocated being one 3072-byte
// block; and a number is half a PDP-11's rather than a sixth, v7's block having been 512
// bytes.  THE MULTIPLY IS AT THE printf AND NOWHERE ELSE -- every count in this file is a
// filesystem block until it is printed.  df.1m says so; ../README.md SS4 is the general rule.
//
// WHAT THE DEFAULT LIST IS NOW.  v7's df, and this one until now, had a compiled-in device
// list.  /etc/mtab exists since task C4f (../mount/mtab.c), so the list is the ROOT plus every
// entry in it -- and mtab.c is compiled into this program a THIRD time rather than a second
// parser being written, which is the whole reason that file exists.  The root is synthesised
// because mtab never holds it: kernel mount[0] is claimed by iinit() and mount(1M) never
// names it.
//
// AND WHAT AN ARGUMENT MAY BE.  A special file, as before; a MOUNT POINT or any ordinary file,
// resolved through st_dev, which is Berkeley's behaviour; or a filesystem IMAGE, which is
// neither's and is how the b6sim cases run at all (../df/test/).
//
// THE THREE MACHINE RULES THIS FILE STILL TURNS ON, all of them from the raw device:
//
//  1. `bno<<BSHIFT' does not compile, and that is the point.  There IS no BSHIFT in this
//     port and there cannot be -- BSIZE is 3072 and 3072 is not a power of two --
//     sys/param.h says it outright.  It is a multiply.
//
//  2. NO %D, NO %ld, NO UNSIGNED.  doprnt.c does not know the PDP-11 spelling of %D, and an
//     unknown conversion is echoed verbatim AND consumes no argument, so one would
//     desynchronise every later conversion in the same format.  RetroBSD's %ld/%lu/%u are the
//     same hazard one step on: a long is one 48-bit word here, so every one of them is %d --
//     and its `(unsigned long)used * 100 / availblks' would lower to b$umul and b$udiv, where
//     the signed spelling stays inline and is exact, nothing here reaching 10^6.
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
//     filesystem is still writing through -- and s_tfree in particular lives in core, in the
//     mount table's own buffer, until update() writes it back.
//
// NOT SETUID, AND IT NEVER WAS.  A setuid df would hand the whole filesystem to anybody who
// could think of an octal offset, which is as true as ever -- so the fix was to stop needing
// /dev/rmd0 rather than to lend it out.  The read route is still the super-user's and open(2)
// is still the whole of its gate.  ../README.md SS8 is the rule.
//

// The order here does not matter and cannot be made to: clang-format sorts a block of <>
// includes alphabetically, so the on-disk-layout headers arrive ahead of the sys/param.h
// and sys/types.h they need.  They include what they depend on themselves -- see the note
// at the head of each, and sys/dir.h, which is the precedent.
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fblk.h>
#include <sys/filsys.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/types.h>
#include <unistd.h>

#include "mtab.h"

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

// The root's special file.  A #define and not a `char *' in a table, because a string literal
// cannot initialise a char * inside a struct initialiser on this compiler (../README.md).
// rootdev is makedev(0, 0) -- ../../root.manifest, bdevsw[0] -- there is one EC-5052 drive and
// there are no partitions, and the kernel exports no way to ask which device it booted from.
#define ROOTSPEC "/dev/md0"
#define ROOTDIR  "/"

// What a row prints where there is no mount point to name.  Not "", which would leave a row
// ending in blanks: these lines are diffed literally by four tests and trailing whitespace is
// what an editor eats.  A dash is also a field awk can see.
#define NOMNT "-"

// The table of filesystems this machine knows about: the root, plus /etc/mtab.
#define NDF (NMTAB + 1)
_Static_assert(NDF >= NMTAB + 1, "the root needs a slot of its own above /etc/mtab's");

struct dfs {
    char s_spec[NAMSIZ]; // the special file, as the Filesystem column prints it
    char s_dir[NAMSIZ];  // where it is mounted
    dev_t s_dev;         // stat(s_dir).st_dev, or NODEV if it could not be had
};

// One row, resolved but not yet read.  e_name is what the Filesystem column prints and
// e_read is what open(2) is actually given -- the RAW twin of a block device, where there is
// one, because the raw path is the one README.md's four rules are about.
//
// e_mounted picks the route, and is set only where the row was found THROUGH a mount point:
// the default list and resolve()'s st_dev arm.  statfs(2) is given e_dir, not e_name -- the
// kernel wants a path it can walk, and NOMNT ("-") is not one.
struct dfent {
    char e_name[NAMSIZ];
    char e_dir[NAMSIZ];
    char e_read[NAMSIZ];
    int e_mounted;
};

// FILE SCOPE, NOT AUTOMATIC.  A long function costs 1.5-2 words of frame per source line
// before any array (../README.md SS6), and there is no reason for either of these to be on
// the stack: df is not reentrant and never holds two rows at once.
static struct dfs fstab[NDF];
static struct dfent ent;
static int nfstab;
static int tabloaded;

static int iflag; // -i: the i-node columns
static int wflag; // -w: walk the free list instead of reading s_tfree

static daddr_t blkno = 1;

static struct filsys sblock;

static int fi = -1;

// The one read target, and the only buffer any raw read here names.  BSIZEW words of it
// are used; the rest is the slack the alignment step needs, since where bss lands is the
// linker's business and nothing in C can ask for a 512-word boundary.
static int rawbuf[BSIZEW + MDALIGN];
static int *blk;

static void usage(void);
static void loadtab(void);
static char *dirfor(const char *spec);
static int rawtwin(char *dst, const char *src);
static int isimage(const char *file);
static int resolve(const char *arg, struct dfent *e, int quiet);
static int getnums(struct dfent *e, daddr_t *total, daddr_t *avail, ino_t *files, ino_t *ifree);
static void report(struct dfent *e, int maxwidth);
static void prtstat(struct dfent *e, daddr_t total, daddr_t avail, ino_t files, ino_t ifree,
                    int maxwidth);
static int sbread(struct dfent *e);
static daddr_t walkfree(void);
static daddr_t alloc(void);
static int bread(daddr_t bno);

int main(int argc, char **argv)
{
    int i, n, maxwidth;
    char *p;

    // An `int *' IS a word address on this machine -- lib/test/memt.c casts one the same
    // way -- so the alignment is ordinary arithmetic.  A loop rather than a mask because
    // it runs once, and because a mask over a negation would depend on how a negative
    // word is spelled here.
    blk = rawbuf;
    while ((int)blk % MDALIGN != 0)
        blk++;

    // There is no getopt(3) in this libc; icheck.c's loop is the idiom.  A lone `-' is a
    // filename and not a flag word, which is why argv[1][1] has to be looked at.
    while (argc > 1 && argv[1][0] == '-' && argv[1][1] != 0) {
        for (p = &argv[1][1]; *p != 0; p++) {
            if (*p == 'i')
                iflag = 1;
            else if (*p == 'w')
                wflag = 1;
            else
                usage();
        }
        argc--;
        argv++;
    }

    maxwidth = 0;

    if (argc <= 1) {
        // The default list, and the only path in this program that reads /etc/mtab.
        loadtab();
        for (i = 0; i < nfstab; i++) {
            n = (int)strlen(fstab[i].s_spec);
            if (n > maxwidth)
                maxwidth = n;
        }
        for (i = 0; i < nfstab; i++) {
            strcpy(ent.e_name, fstab[i].s_spec);
            strcpy(ent.e_dir, fstab[i].s_dir);
            if (!rawtwin(ent.e_read, fstab[i].s_spec))
                strcpy(ent.e_read, fstab[i].s_spec);
            // Every row here came out of a mount point, so the default report is asked.
            ent.e_mounted = 1;
            report(&ent, maxwidth);
        }
        return 0;
    }

    // TWO PASSES, and the first is SILENT.  Berkeley sizes the first column to the widest
    // name it is going to print, which cannot be known before every argument has been
    // resolved; resolving one can also produce a diagnostic, and a diagnostic printed twice
    // is worse than a column one character wide.  The cost is one extra stat(2) per
    // argument, and for an image argument one extra block read.
    for (i = 1; i < argc; i++) {
        if (resolve(argv[i], &ent, 1)) {
            n = (int)strlen(ent.e_name);
            if (n > maxwidth)
                maxwidth = n;
        }
    }
    for (i = 1; i < argc; i++)
        if (resolve(argv[i], &ent, 0))
            report(&ent, maxwidth);
    return 0;
}

// A LITERAL, never argv[0]: under b6sim argv[0] is the staged absolute path of the program
// in the build tree, so a usage line naming it could not be diffed (../README.md SS9).
static void usage(void)
{
    fprintf(stderr, "usage: df [-iw] [ filesystem | file ] ...\n");
    exit(1);
}

//
// The filesystem table: the root, then /etc/mtab.  LAZY AND IDEMPOTENT, and both matter.
//
// Idempotent because resolve() may want it once per argument.  Lazy because of the b6sim
// harness: every system call there is serviced on the HOST, so mtabread() run unconditionally
// would read the BUILD MACHINE's /etc/mtab -- absent on macOS, and on Linux a symlink to
// /proc/self/mounts, which would also put mtab.c's "more than 8 entries" warning on stderr.
// mount(1M) diverges from v7 in exactly this way and for exactly this reason: settle every
// argument before opening the table.  ../mount/test/CMakeLists.txt is the account, and
// test/CMakeLists.txt beside this file says which cases may reach here (none).
//
// THE ROOT IS SYNTHESISED because /etc/mtab never holds it: kernel mount[0] is claimed by
// iinit() and mount(1M) has nothing to record.  A missing /etc/mtab is an empty table and not
// an error (mtab.c), so on a machine that has mounted nothing this is one entry.
//
static void loadtab(void)
{
    struct stat st;
    int i;

    if (tabloaded)
        return;
    tabloaded = 1;

    strcpy(fstab[0].s_spec, ROOTSPEC);
    strcpy(fstab[0].s_dir, ROOTDIR);
    fstab[0].s_dev = stat(ROOTDIR, &st) < 0 ? NODEV : st.st_dev;
    nfstab         = 1;

    mtabread();
    for (i = 0; i < nmtab && nfstab < NDF; i++) {
        strcpy(fstab[nfstab].s_spec, mtab[i].m_spec);
        strcpy(fstab[nfstab].s_dir, mtab[i].m_dir);
        // The dev_t of a MOUNTED filesystem, taken through its mount point: namei() crosses
        // the mount, so this is the mounted device and not the directory it covers.  That is
        // what lets `df /some/file' find its filesystem with no name arithmetic at all.
        fstab[nfstab].s_dev = stat(mtab[i].m_dir, &st) < 0 ? NODEV : st.st_dev;
        nfstab++;
    }
}

//
// Where a special file is mounted, or NOMNT.  BY NAME and not by st_rdev, because the raw
// and block nodes of one drive have different majors here -- cdevsw[3] against bdevsw[0] --
// so their device numbers say nothing about each other.  The raw twin is tried as well, which
// is what makes `df /dev/rmd0' say `/' when the table records /dev/md0.
//
static char *dirfor(const char *spec)
{
    char raw[NAMSIZ];
    int i, haveraw;

    loadtab();
    haveraw = rawtwin(raw, spec);
    for (i = 0; i < nfstab; i++) {
        if (strcmp(fstab[i].s_spec, spec) == 0)
            return fstab[i].s_dir;
        if (haveraw && strcmp(raw, fstab[i].s_spec) == 0)
            return fstab[i].s_dir;
        if (!haveraw && rawtwin(raw, fstab[i].s_spec) && strcmp(raw, spec) == 0)
            return fstab[i].s_dir;
    }
    return NOMNT;
}

//
// "/dev/md1" -> "/dev/rmd1": an `r' in front of the LAST path component.  Returns 0 if the
// result would not fit, and if the name already begins that component with an `r', so that
// this is idempotent-ish and dirfor() can ask in both directions.
//
static int rawtwin(char *dst, const char *src)
{
    int i, last, n;

    last = 0;
    for (i = 0; src[i] != 0; i++)
        if (src[i] == '/')
            last = i + 1;
    n = i;
    if (n + 1 >= NAMSIZ || src[last] == 0 || src[last] == 'r')
        return 0;
    for (i = 0; i < last; i++)
        dst[i] = src[i];
    dst[last] = 'r';
    for (i = last; i < n; i++)
        dst[i + 1] = src[i];
    dst[n + 1] = 0;
    return 1;
}

//
// Does this ordinary file hold a filesystem?  A whole-block read at a block-aligned offset is
// legal against a regular file too, so this is the device path's own bread() and the same
// buffer.  It is what lets `df fsimg.img' work -- neither v7's df nor Berkeley's has such a
// case, and it is how the b6sim cases test this program against real on-disk structure
// without touching a device or the host's /etc/mtab (README.md).
//
// SILENT, which is why it does not go through bread(): an ordinary file shorter than a block
// is the common case here and is not an error, it is the answer `no'.
static int isimage(const char *file)
{
    int fd, ok;

    fd = open(file, O_RDONLY);
    if (fd < 0)
        return 0;
    lseek(fd, (off_t)SUPERB * BSIZE, SEEK_SET);
    ok = read(fd, (char *)blk, BSIZE) == BSIZE && ((struct filsys *)blk)->s_magic == FS_MAGIC;
    close(fd);
    return ok;
}

//
// One argument -> one row.  `quiet' suppresses the diagnostics, for the width pass.
//
static int resolve(const char *arg, struct dfent *e, int quiet)
{
    struct stat st;
    int i, mode;

    if (stat(arg, &st) < 0) {
        // v7's wording, and v7's exit status: df goes on to the next argument and still
        // exits 0.  NO TABLE IS LOADED on this path, which is what keeps the nodev case
        // testable under b6sim.
        if (!quiet)
            fprintf(stderr, "cannot open %s\n", arg);
        return 0;
    }
    mode = st.st_mode & S_IFMT;

    // A named device is READ even when it is mounted: answering it out of the mount table
    // would make the one command that can disagree with the kernel agree by construction,
    // and kernel/test/fsinfo needs the two readings independent.
    e->e_mounted = 0;

    if (mode == S_IFCHR) {
        strcpy(e->e_name, arg);
        strcpy(e->e_read, arg);
        strcpy(e->e_dir, dirfor(arg));
        return 1;
    }
    if (mode == S_IFBLK) {
        strcpy(e->e_name, arg);
        if (!rawtwin(e->e_read, arg))
            strcpy(e->e_read, arg);
        strcpy(e->e_dir, dirfor(arg));
        return 1;
    }
    if (mode == S_IFREG && isimage(arg)) {
        strcpy(e->e_name, arg);
        strcpy(e->e_read, arg);
        strcpy(e->e_dir, NOMNT);
        return 1;
    }

    // A mount point, or any file on a mounted filesystem.  Berkeley's behaviour, reached
    // through st_dev rather than through statfs(2).
    loadtab();
    for (i = 0; i < nfstab; i++) {
        if (fstab[i].s_dev != NODEV && fstab[i].s_dev == st.st_dev) {
            strcpy(e->e_name, fstab[i].s_spec);
            strcpy(e->e_dir, fstab[i].s_dir);
            if (!rawtwin(e->e_read, fstab[i].s_spec))
                strcpy(e->e_read, fstab[i].s_spec);
            e->e_mounted = 1;
            return 1;
        }
    }
    if (!quiet)
        fprintf(stderr, "%s: not a mounted filesystem\n", arg);
    return 0;
}

//
// The four numbers, by whichever route this row takes; 0 if it cannot be reported on.
//
// The arithmetic is written ONCE and is the same on both routes -- the fields mean what they
// mean whichever side they came from, so the corrected i-node count must not be re-derived.
// -w takes the read route for every argument, which is not a special case: the walk IS the
// superblock reader, and one served out of the mount table would be measuring nothing.
//
static int getnums(struct dfent *e, daddr_t *total, daddr_t *avail, ino_t *files, ino_t *ifree)
{
    struct statfs sf;

    if (e->e_mounted && !wflag) {
        if (statfs(e->e_dir, &sf) < 0) {
            fprintf(stderr, "cannot stat %s\n", e->e_name);
            return 0;
        }
        *total = sf.f_fsize - sf.f_isize;
        *avail = sf.f_tfree;
        *files = ((ino_t)sf.f_isize - (SUPERB + 1)) * INOPB;
        *ifree = sf.f_tinode;
        return 1;
    }

    if (!sbread(e))
        return 0;
    *total = sblock.s_fsize - sblock.s_isize;
    *avail = sblock.s_tfree;
    *files = ((ino_t)sblock.s_isize - (SUPERB + 1)) * INOPB;
    *ifree = sblock.s_tinode;
    return 1;
}

//
// One row: get the six numbers and print.
//
// THE ORDER IS LOAD-BEARING.  Every number comes out of sblock BEFORE walkfree() runs,
// because alloc() drains s_nfree and overwrites s_free[] as it pops.
//
// Neither the superblock nor the i-list is in any column: what df is about is the DATA area,
// which is what a file can be put in.  b6fsutil's "N blocks in use, M free" counts the same
// two quantities, which is what kernel/test/fsinfo holds this against.
//
static void report(struct dfent *e, int maxwidth)
{
    daddr_t total, avail, walked;
    ino_t files, ifree;

    if (!getnums(e, &total, &avail, &files, &ifree))
        return;

    if (wflag) {
        walked = walkfree();
        if (walked != avail)
            fprintf(stderr, "%s: the free list has %d blocks, the superblock says %d\n", e->e_name,
                    walked, avail);
        avail = walked;
    }
    if (avail < 0)
        avail = 0;

    prtstat(e, total, avail, files, ifree, maxwidth);

    // Only the read route left a descriptor open; the asked route never had one.
    if (fi >= 0) {
        close(fi);
        fi = -1;
    }
}

//
// The table.  RetroBSD's format strings, with %ld/%lu/%u spelled %d and the fsbtoblk()
// scaling replaced by the one `* KBPB' this port allows itself, at the printf and nowhere
// else (../README.md SS4).  The column geometry, including the two places a number sits left
// of its own header, is 4.4BSD's and is reproduced rather than tidied.
//
static void prtstat(struct dfent *e, daddr_t total, daddr_t avail, ino_t files, ino_t ifree,
                    int maxwidth)
{
    static int printed;
    daddr_t used, availblks;
    ino_t iused;
    int pct, ipct;

    if (maxwidth < 11)
        maxwidth = 11;
    if (!printed) {
        printed = 1;
        printf("%-*.*s %s     Used    Avail Capacity", maxwidth, maxwidth, "Filesystem",
               "1K-blocks");
        if (iflag)
            printf(" iused   ifree  %%iused");
        printf("  Mounted on\n");
    }

    used = total - avail;
    // RetroBSD's `f_bavail + used', kept in its own shape though it degenerates: a v7
    // filesystem has no BSD minfree, so nothing is withheld from an ordinary user and this
    // is just `total'.  The portable spelling is the one written down.
    availblks = avail + used;
    pct       = availblks == 0 ? 100 : used * 100 / availblks;

    printf("%-*.*s", maxwidth, maxwidth, e->e_name);
    printf(" %9d %8d %8d", total * KBPB, used * KBPB, avail * KBPB);
    printf(" %5d%%", pct);
    if (iflag) {
        iused = files - ifree;
        ipct  = files == 0 ? 100 : iused * 100 / files;
        printf(" %7d %7d %5d%% ", iused, ifree, ipct);
    } else
        printf("  ");
    printf("  %s\n", e->e_dir);
}

//
// Open the filesystem and read its superblock, validated.
//
// THE RAW TWIN FIRST, THE NAME AS GIVEN SECOND, and the fallback has a live case: there is no
// /dev/rmd3 on this image -- only rmd0 and rmd1 are staged -- so `df /dev/md3' reads the BLOCK
// device, through readi() and the buffer cache, which has no alignment rule at all.  That is
// what this program has been doing on that path all along.  Preferring the raw node is what
// keeps README.md's four conditions the thing a bare `df' obeys.
//
static int sbread(struct dfent *e)
{
    static int synced;
    int j;
    int *sp, *dp;

    fi = open(e->e_read, O_RDONLY);
    if (fi < 0 && strcmp(e->e_read, e->e_name) != 0)
        fi = open(e->e_name, O_RDONLY);
    if (fi < 0) {
        // The permission case gets its own words: it is the only wall an ordinary user
        // meets now, and "cannot open" would not say what to do instead.
        if (errno == EACCES || errno == EPERM)
            fprintf(stderr, "%s: the raw device is the super-user's; name a file or a mount point\n",
                    e->e_name);
        else
            fprintf(stderr, "cannot open %s\n", e->e_name);
        return 0;
    }

    // Load-bearing on the raw path, and once per run is enough; see the header.  s_tfree in
    // particular is in core until update() writes it back.
    if (!synced) {
        synced = 1;
        sync();
    }

    // The superblock is exactly one block, so this is a whole-block read by construction.
    // The copy out of the shared buffer is what lets alloc() read chain blocks into it
    // afterwards without losing the superblock it is walking.  A word loop rather than
    // memcpy(): both objects are word-aligned here, and memcpy would step 3072 fat-pointer
    // bytes to move 512 words.
    if (!bread(SUPERB)) {
        close(fi);
        fi = -1;
        return 0;
    }
    sp = blk;
    dp = (int *)&sblock;
    for (j = 0; j < BSIZEW; j++)
        dp[j] = sp[j];

    // THE GEOMETRY WORDS, which v7's df does not look at and every other tool in this
    // directory does.  sbcheck() (kernel/alloc.c) refuses to mount a superblock that fails
    // these and fsck(1M) refuses to repair one; a df that reported on one anyway would be the
    // odd one out, and would print arithmetic on somebody's tar archive.  Both tests are
    // EQUALITIES, so they stay inline -- a relational on this machine is an out-of-line call.
    if (sblock.s_magic != FS_MAGIC) {
        fprintf(stderr, "%s: not a filesystem\n", e->e_name);
        close(fi);
        fi = -1;
        return 0;
    }
    if (sblock.s_bsize != BSIZEW || sblock.s_inopb != INOPB || sblock.s_naddr != NADDR) {
        fprintf(stderr, "%s: filesystem geometry mismatch\n", e->e_name);
        fprintf(stderr, "bsize %d inopb %d naddr %d; this system wants %d %d %d\n", sblock.s_bsize,
                sblock.s_inopb, sblock.s_naddr, BSIZEW, INOPB, NADDR);
        close(fi);
        fi = -1;
        return 0;
    }
    return 1;
}

// v7's count: pop the whole free list and say how long it was.  -w only.
static daddr_t walkfree(void)
{
    daddr_t n;

    blkno = 1;
    n     = 0;
    while (alloc())
        n++;
    return n;
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
        if (!bread(b))
            return 0;
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
//
// It RETURNS rather than exiting, which v7's does not (and v7's exited 0 at that, a failure
// reporting success).  df now has a table to print: one unreadable filesystem in it must cost
// its own row and not the rest of the report.
static int bread(daddr_t bno)
{
    int n;

    lseek(fi, (off_t)bno * BSIZE, SEEK_SET);
    if ((n = read(fi, (char *)blk, BSIZE)) != BSIZE) {
        fprintf(stderr, "read error %d\n", bno);
        fprintf(stderr, "count = %d; errno = %d\n", n, errno);
        return 0;
    }
    return 1;
}
