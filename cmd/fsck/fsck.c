/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// fsck -- file system consistency check and interactive repair.
//
//	/etc/fsck [ -y ] [ -n ] [ -s ] [ -S ] filesystem ...
//
// THE FIRST PROGRAM HERE THAT CAN REPAIR A FILESYSTEM.  Task C4a gave the machine df, du
// and quot, which measure its store; C4b gave dd, which moves it; C4c gave mkfs, which
// makes one.  Until this program it was still b6fsutil on the HOST that checked everything
// those wrote.  Its opposite number is cmd/fsutil/check.cpp, which implements the same
// checks there, and the two are each other's oracle: cmd/fsck/test/ damages an image with
// `b6fsutil -D', has this program repair it, and requires `b6fsutil -c' to then find
// nothing.  ../fsck/README.md is the account of what that comparison turned up.
//
// THE MEMORY MANAGEMENT IS GONE, and it is most of what shrank.  v7 sized an arena from
// MAXDATA -- which is #ifdef pdp11/i386/vax/interdata, so this file did not compile at all
// -- and when the arena would not hold the maps it fell back to a SCRATCH FILE, with a
// buffer pool, an LRU search(), and a second arm in each of domap()/dostate()/dolncnt()
// reading a map through that pool.  All of it exists because a PDP-11 had 54 Kb.  This
// machine's drive is 2000 blocks (kernel/dev/md.c) and a default i-list is one inode per
// two blocks, so every map together is about 1,300 words: they are calloc'd from the
// superblock's own numbers, a failure is a refusal rather than a fallback, and the pool,
// the scratch file, the -t option and the second arm of three functions all go.  ../mkfs
// lost its prototype language the same way and for the same reason.
//
// THE RAW DEVICE is the other half.  A transfer through /dev/rmd0 goes physio() ->
// mdstrategy() and those two impose four conditions v7 knows nothing about, listed in
// ../df/README.md; two of them are conditions v7's fsck BREAKS.  It read into sbrk'd
// memory at whatever alignment that gave, and its i-list cache read NINOBLK..MAXRAW blocks
// -- 11 to 110 -- at a time into the middle of that arena.  Here every read and every
// write is ONE BSIZE BLOCK into one of four buffers cut from a single aligned bss array,
// and there is no i-list cache: ginode() reads the block the inode is in, as v7 does when
// its own raw path is off.  The fifth condition, the one only a write can break -- physio()
// refuses a base below u.u_tsize, so a raw write may never be sourced from the text -- is
// ../mkfs/README.md's, and this program cannot break it either: every buffer is bss.
//
// FOUR BUFFERS, NOT FIVE, and the one that is not there is worth knowing about.  v7's
// iblock() declares `BUFAREA ib' as an AUTOMATIC, which is 515 words of frame per level of
// indirection against a 4,096-word stack nothing checks (../README.md SS6).  An aligned
// buffer cannot be an automatic here, so the obvious fix is one static per level -- and it
// is wrong: pass2 descends into a subdirectory from inside dirscan(), which is inside the
// PARENT's iblock(), so two walks of the same level are live at once and would share it.
// What is here instead is one shared indirect buffer re-fetched on every iteration, which
// is exactly the idiom v7's own dirscan() already uses on fileblk for exactly this reason.
// getblk() returns immediately when the block is still there, so it costs nothing.
//
// THE ON-DISK SHAPE DIFFERS FROM v7's IN FOUR PLACES, and each deletes code:
//
//   * NADDR is 8 and NLEVEL is 2 -- six direct, one single, one double, and no triple.
//     Carried unchanged, v7's `&iaddrs[NADDR-3]' and `for(n=1;n<4;n++)' would have read
//     direct block di_addr[5] as an indirect block.
//   * A daddr_t is a whole word, so di_addr is daddr_t[NADDR] and l3tol() -- which this
//     libc does not have, being a PDP-11 artefact -- is a copy loop.  The local copy
//     itself STAYS: descend() reloads inoblk through ginode() and would invalidate dp.
//   * There is no BSHIFT and no BMASK.  A block is 3072 bytes, which is not a power of
//     two, so every `blk<<BSHIFT' is a multiply and every `x&BMASK' a remainder.
//   * struct filsys has no s_m/s_n and no s_fname/s_fpack (sys/filsys.h).  So the
//     "File System:/Volume:" line goes, and phase 6 rebuilds the free list the way
//     ../mkfs/mkfs.c builds it -- descending, no cylinder interleave -- which deletes
//     stype(), the -s/-S ARGUMENT, and a 584-word stack frame (flg[500] + addr[500]).
//
// s_tfree AND s_tinode ARE CHECKED AND REPAIRED, as v7 checks and repairs them.  For most
// of this port's life they were dead -- mkfs set them and the kernel maintained neither, so
// on any volume that had been written to they were stale by construction, and this program
// could only NOTE them.  kernel/alloc.c maintains both now, at the four points that change
// them, so a mismatch is a real inconsistency again and is offered as a FIX.  Both formulas
// are recomputed from this run's own walk; cmd/fsutil/check.cpp faults an image on either,
// which is what lets the two checkers be held against each other over them.
//
// WHAT IT REPORTS IN.  The summary and "N BLK(S) MISSING" are MEASUREMENTS, so they are
// printed in 1024-byte blocks -- KBPB of them per filesystem block, ../README.md SS4 --
// and the free count can therefore be compared with df(1M)'s, which kernel/test/fsck does.
// A block NUMBER in a diagnostic is not a measurement and stays exactly as it is on the
// disk, and neither is a count of events ("N BAD BLKS IN FREE LIST").  fsck.1m.umm has the
// section SS4 requires.
//
// NO /etc/checklist, the one deliberate divergence in the argument handling: v7 with no
// argument reads a list of filesystems from that file, and this system has one filesystem
// and no such file.  A device argument is required.  fsck.1m.umm says so.
//
// NOT SETUID, and it must not become so, for /etc/mkfs's and /etc/quot's reason: /dev/rmd0
// is mode 0600 because that one node is every file's contents, and this program writes it.
// ../README.md SS8.
//

#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
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

// getpw(3) declares itself at its caller, as v7 left it and as getpass(3) does.
int getpw(int uid, char buf[]);

// What this file's arithmetic assumes of the layout, asserted rather than re-derived
// (../TODO.md, task C4).  The headers carry the rest.
_Static_assert(BSIZE == BSIZEW * NBPW, "a block must be BSIZEW words of NBPW bytes");
_Static_assert(BSIZE % KBYTE == 0, "a block must be a whole number of reported blocks");
_Static_assert(1 + NICFREE <= BSIZEW, "a chain block must fit the block buffer");
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

#define MAXDUP 10 // limit on dup blks (per inode)
#define MAXBAD 10 // limit on bad blks (per inode)

#define BITSPB   8 // bits per byte in the block maps
#define BITSHIFT 3 // log2(BITSPB)
#define BITMASK  07

#define LSTATE  2 // bits per inode state
#define STATEPB (BITSPB / LSTATE)
#define USTATE  0  // inode not allocated
#define FSTATE  01 // inode is file
#define DSTATE  02 // inode is directory
#define CLEAR   03 // inode is to be cleared
#define SMASK   03

#define DUPTBLSIZE 100 // num of dup blocks to remember
#define MAXLNCNT   20  // num zero link cnts to remember

// The path built during phase 2, and the bound v7 did not have.  It accumulates one
// component per directory level with no check of any kind in the original -- ../README.md
// SS6's "every port so far has had to bound one".  The room left is measured by SUBTRACTING
// two char pointers, never by comparing them: SS2, and `-' is the operation that works.
#define PATHLEN  256
#define PATHROOM (PATHLEN - 1 - (int)(pathp - pathname))

typedef struct dinode DINODE;
typedef struct direct DIRECT;

#define ALLOC   ((dp->di_mode & S_IFMT) != 0)
#define DIR     ((dp->di_mode & S_IFMT) == S_IFDIR)
#define BLK     ((dp->di_mode & S_IFMT) == S_IFBLK)
#define CHR     ((dp->di_mode & S_IFMT) == S_IFCHR)
#define MPC     ((dp->di_mode & S_IFMT) == S_IFMPC)
#define MPB     ((dp->di_mode & S_IFMT) == S_IFMPB)
#define SPECIAL (BLK || CHR || MPC || MPB)

// A block buffer.  v7 wrapped a union of every shape a block can have; here the storage is
// an aligned int array outside the struct (it has to be -- see the head comment) and the
// shapes are casts through b_addr.  df.c reads a chain block through the same cast.
typedef struct bufarea {
    daddr_t b_bno;
    int *b_addr;
    int b_dirty;
} BUFAREA;

#define NBUFS 4 // sblk, fileblk, inoblk, indblk

// The one transfer area.  NBUFS blocks of BSIZEW words, plus the slack the alignment step
// needs: where bss lands is the linker's business and nothing in C can ask for a 512-word
// boundary.  Consecutive slots stay aligned, one block being exactly MDALIGN words.
static int rawbuf[NBUFS * BSIZEW + MDALIGN];

static BUFAREA sblk;    // the superblock
static BUFAREA fileblk; // a directory block, or a free-list chain block
static BUFAREA inoblk;  // a block of the i-list
static BUFAREA indblk;  // an indirect block, at any level -- see the head comment

#define initbarea(x) ((x)->b_dirty = 0, (x)->b_bno = (daddr_t)-1)
#define inodirty()   (inoblk.b_dirty = 1)
#define fbdirty()    (fileblk.b_dirty = 1)
#define sbdirty()    (sblk.b_dirty = 1)

#define superblk (*(struct filsys *)sblk.b_addr)
#define freeblk  (*(struct fblk *)fileblk.b_addr)
#define dirblk   ((DIRECT *)fileblk.b_addr)
#define inodes   ((DINODE *)inoblk.b_addr)

// The device.  One name, two descriptors -- v7 opened the same path twice, and so does
// this: a read-only open cannot be upgraded and -n must not acquire the write one at all.
static int rfd = -1;
static int wfd = -1;
static int modified; // anything written?  drives the closing banner

static daddr_t duplist[DUPTBLSIZE];
static daddr_t *enddup;
static daddr_t *muldup;

static ino_t badlncnt[MAXLNCNT];
static ino_t *badlnp;

static int sflag;    // salvage free block list
static int csflag;   // salvage free block list (conditional)
static int nflag;    // assume a no response
static int yflag;    // assume a yes response
static int rplyflag; // any questions asked?
static int hotroot;  // checking the mounted root
static int fixfree;  // corrupted free list

static char *blkmap;   // one bit per block: seen in a file
static char *freemap;  // one bit per block: seen in the free list
static char *statemap; // two bits per inode
static int *lncntp;    // one link count per inode

static char *pathp;    // where the next component goes
static char *thisname; // the component being examined
static char *srchname; // name being searched for in a directory
static char pathname[PATHLEN];
static const char *lfname = "lost+found";

static int badblk; // num of bad blks seen (per inode)
static int dupblk; // num of dup blks seen (per inode)

// The two walk callbacks.  v7 had ONE untyped `int (*pfunc)()' called with a daddr_t by
// the ADDR walks and with a DIRECT * by the DATA walks; C11 has no such pointer, and
// this splits it rather than casting at every call.  Which one is live is decided by
// ckinode()'s flg, exactly as it was.
static int (*pfunc)(daddr_t blk);
static int (*dfunc)(DIRECT *dirp);

static ino_t inum;      // inode we are currently working on
static ino_t imax;      // number of inodes
static ino_t parentdir; // i number of parent directory
static ino_t lastino;   // hiwater mark of inodes
static ino_t lfdir;     // lost & found directory
static ino_t orphan;    // orphaned inode

static off_t filsize; // num blks seen in file
static off_t bmapsz;  // num chars in a block map

static daddr_t n_free;  // number of free blocks
static daddr_t n_blks;  // number of blocks used
static daddr_t n_files; // number of files seen
static daddr_t fmin;    // block number of the first data block
static daddr_t fmax;    // number of blocks in the volume

#define howmany(x, y) (((x) + ((y) - 1)) / (y))
#define roundup(x, y) ((((x) + ((y) - 1)) / (y)) * (y))
#define outrange(x)   ((x) < fmin || (x) >= fmax)
#define zapino(x)     clear((char *)(x), sizeof(DINODE))

#define setlncnt(x) dolncnt(x, 0)
#define getlncnt()  dolncnt(0, 1)
#define declncnt()  dolncnt(0, 2)

#define setbmap(x) domap(x, 0)
#define getbmap(x) domap(x, 1)
#define clrbmap(x) domap(x, 2)

#define setfmap(x) domap(x, 0 + 4)
#define getfmap(x) domap(x, 1 + 4)

#define setstate(x) dostate(x, 0)
#define getstate()  dostate(0, 1)

#define DATA   1
#define ADDR   0
#define ALTERD 010
#define KEEPON 04
#define SKIP   02
#define STOP   01

static void check(char *dev);
static int setup(char *dev);
static int ckinode(DINODE *dp, int flg);
static int iblock(daddr_t blk, int ilevel, int flg);
static int pass1(daddr_t blk);
static int pass1b(daddr_t blk);
static int pass2(DIRECT *dirp);
static int pass4(daddr_t blk);
static int pass5(daddr_t blk);
static int findino(DIRECT *dirp);
static int mkentry(DIRECT *dirp);
static int chgdd(DIRECT *dirp);
static int dirscan(daddr_t blk);
static void blkerr(const char *s, daddr_t blk);
static void descend(void);
static int direrr(const char *s);
static void adjust(int lcnt);
static void clri(const char *s, int flg);
static DINODE *ginode(void);
static int ftypeok(DINODE *dp);
static int reply(const char *s);
static int getline(FILE *fp, char *loc, int maxlen);
static int dostate(int s, int flg);
static int domap(daddr_t blk, int flg);
static int dolncnt(int val, int flg);
static BUFAREA *getblk(BUFAREA *bp, daddr_t blk);
static void flush(BUFAREA *bp);
static void rwerr(const char *s, daddr_t blk);
static void sizechk(DINODE *dp);
static void ckfini(void);
static void pinode(void);
static void copy(char *fp, char *tp, int size);
static void freechk(void);
static void makefree(void);
static void clear(char *p, int cnt);
static int linkup(void);
static int israwroot(const char *dev, dev_t rootdev);
static int bread(int *buf, daddr_t blk);
static int bwrite(int *buf, daddr_t blk);
static void catch(int sig);
static void errexit(void);

int main(int argc, char **argv)
{
    int *p;
    int i;

    // The four block buffers.  An `int *' IS a word address on this machine, so the
    // alignment is ordinary arithmetic; a loop rather than a mask because it runs once and
    // because a mask over a negation would depend on how a negative word is spelled here.
    // df.c, quot.c and mkfs.c all step the same way.
    p = rawbuf;
    while ((int)p % MDALIGN != 0)
        p++;
    sblk.b_addr    = p;
    fileblk.b_addr = p + BSIZEW;
    inoblk.b_addr  = p + 2 * BSIZEW;
    indblk.b_addr  = p + 3 * BSIZEW;

    sync();

    while (--argc > 0 && **++argv == '-') {
        switch ((*argv)[1]) {
        case 's': // salvage the free list
        case 'S': // ... only if nothing else was wrong
            if ((*argv)[1] == 's')
                sflag++;
            else
                csflag++;
            break;
        case 'n':
        case 'N':
            nflag++;
            yflag = 0;
            break;
        case 'y':
        case 'Y':
            yflag++;
            nflag = 0;
            break;
        default:
            printf("%c option?\n", (*argv)[1]);
            errexit();
        }
    }
    if (nflag && (sflag || csflag)) {
        printf("Incompatible options: -n and -%s\n", sflag ? "s" : "S");
        errexit();
    }
    if (sflag && csflag)
        sflag = 0;

    // v7 with no argument read /etc/checklist.  There is no such file here and one
    // filesystem to name; fsck.1m.umm records the divergence.
    if (argc <= 0) {
        fprintf(stderr, "usage: /etc/fsck [ -y ] [ -n ] [ -s ] [ -S ] filesystem ...\n");
        return 1;
    }

    if (signal(SIGINT, SIG_IGN) != SIG_IGN)
        signal(SIGINT, catch);

    for (i = 0; i < argc; i++)
        check(argv[i]);

    return 0;
}

//
// One filesystem, all six phases.
//
static void check(char *dev)
{
    DINODE *dp;
    int n;
    ino_t *blp;
    ino_t savino;
    daddr_t blk;

    if (setup(dev) == NO)
        return;

    printf("** Phase 1 - Check Blocks and Sizes\n");
    pfunc = pass1;
    for (inum = 1; inum <= imax; inum++) {
        if ((dp = ginode()) == NULL)
            continue;
        if (ALLOC) {
            lastino = inum;
            if (ftypeok(dp) == NO) {
                printf("UNKNOWN FILE TYPE I=%d", inum);
                if (reply("CLEAR") == YES) {
                    zapino(dp);
                    inodirty();
                }
                continue;
            }
            n_files++;
            if (setlncnt(dp->di_nlink) <= 0) {
                if (badlnp < &badlncnt[MAXLNCNT])
                    *badlnp++ = inum;
                else {
                    printf("LINK COUNT TABLE OVERFLOW");
                    if (reply("CONTINUE") == NO)
                        errexit();
                }
            }
            setstate(DIR ? DSTATE : FSTATE);
            badblk = dupblk = 0;
            filsize         = 0;
            ckinode(dp, ADDR);
            if ((n = getstate()) == DSTATE || n == FSTATE)
                sizechk(dp);
        } else if (dp->di_mode != 0) {
            printf("PARTIALLY ALLOCATED INODE I=%d", inum);
            if (reply("CLEAR") == YES) {
                zapino(dp);
                inodirty();
            }
        }
    }

    if (enddup != &duplist[0]) {
        printf("** Phase 1b - Rescan For More DUPS\n");
        pfunc = pass1b;
        for (inum = 1; inum <= lastino; inum++) {
            if (getstate() != USTATE && (dp = ginode()) != NULL)
                if (ckinode(dp, ADDR) & STOP)
                    break;
        }
    }

    printf("** Phase 2 - Check Pathnames\n");
    inum     = ROOTINO;
    thisname = pathp = pathname;
    *pathp           = 0;
    dfunc            = pass2;
    switch (getstate()) {
    case USTATE:
        printf("ROOT INODE UNALLOCATED. TERMINATING.\n");
        errexit();
        break;
    case FSTATE:
        printf("ROOT INODE NOT DIRECTORY");
        if (reply("FIX") == NO || (dp = ginode()) == NULL)
            errexit();
        dp->di_mode &= ~S_IFMT;
        dp->di_mode |= S_IFDIR;
        inodirty();
        setstate(DSTATE);
        descend();
        break;
    case DSTATE:
        descend();
        break;
    case CLEAR:
        printf("DUPS/BAD IN ROOT INODE\n");
        if (reply("CONTINUE") == NO)
            errexit();
        setstate(DSTATE);
        descend();
        break;
    }

    printf("** Phase 3 - Check Connectivity\n");
    for (inum = ROOTINO; inum <= lastino; inum++) {
        if (getstate() == DSTATE) {
            dfunc    = findino;
            srchname = "..";
            savino   = inum;
            do {
                orphan = inum;
                if ((dp = ginode()) == NULL)
                    break;
                filsize   = dp->di_size;
                parentdir = 0;
                ckinode(dp, DATA);
                if ((inum = parentdir) == 0)
                    break;
            } while (getstate() == DSTATE);
            inum = orphan;
            if (linkup() == YES) {
                thisname = pathp = pathname;
                *pathp++         = '?';
                *pathp           = 0;
                dfunc            = pass2;
                descend();
            }
            inum = savino;
        }
    }

    printf("** Phase 4 - Check Reference Counts\n");
    pfunc = pass4;
    for (inum = ROOTINO; inum <= lastino; inum++) {
        switch (getstate()) {
        case FSTATE:
            if ((n = getlncnt()))
                adjust(n);
            else {
                for (blp = badlncnt; blp < badlnp; blp++)
                    if (*blp == inum) {
                        clri("UNREF", YES);
                        break;
                    }
            }
            break;
        case DSTATE:
            clri("UNREF", YES);
            break;
        case CLEAR:
            clri("BAD/DUP", YES);
            break;
        }
    }

    // NOTE THE `- 1', WHICH IS NOT v7's ARITHMETIC.  v7 compares against imax - n_files,
    // counting every i-number in the list that is not in use.  INODE 1 IS NOT ONE OF
    // THEM: ialloc() refuses to hand out anything below ROOTINO (kernel/alloc.c), so it
    // is a slot that exists and can never be filled, and ../mkfs/mkfs.c seeds the field
    // as ninodes-2 -- "everything but inode 1, which cannot be allocated, and the root,
    // which is in use" -- as does cmd/fsutil/create.cpp.  Carried unchanged, v7's formula
    // is one too high and this would fire on every clean filesystem this system ever
    // made.  It is the first disagreement between fsck and its oracle that this port
    // found, and fsck was the one that was wrong; ../fsck/README.md has the rest.
    if (imax - n_files - 1 != superblk.s_tinode) {
        printf("FREE INODE COUNT WRONG IN SUPERBLK");
        if (reply("FIX") == YES) {
            superblk.s_tinode = imax - n_files - 1;
            sbdirty();
        }
    }

    flush(&fileblk);

    printf("** Phase 5 - Check Free List ");
    if (sflag || (csflag && rplyflag == 0)) {
        printf("(Ignored)\n");
        fixfree = 1;
    } else {
        printf("\n");
        copy(blkmap, freemap, (int)bmapsz);
        badblk = dupblk  = 0;
        freeblk.df_nfree = superblk.s_nfree;
        for (n = 0; n < NICFREE; n++)
            freeblk.df_free[n] = superblk.s_free[n];
        freechk();
        if (badblk)
            printf("%d BAD BLKS IN FREE LIST\n", badblk);
        if (dupblk)
            printf("%d DUP BLKS IN FREE LIST\n", dupblk);
        if (fixfree == 0) {
            if ((n_blks + n_free) != (fmax - fmin)) {
                printf("%d BLK(S) MISSING\n", (fmax - fmin - n_blks - n_free) * KBPB);
                fixfree = 1;
            } else if (n_free != superblk.s_tfree) {
                // Only when the list itself is sound: if it is about to be salvaged,
                // makefree() lays s_tfree down with it and asking here would be asking
                // twice about one thing.
                printf("FREE BLK COUNT WRONG IN SUPERBLK");
                if (reply("FIX") == YES) {
                    superblk.s_tfree = n_free;
                    sbdirty();
                }
            }
        }
        if (fixfree) {
            printf("BAD FREE LIST");
            if (reply("SALVAGE") == NO)
                fixfree = 0;
        }
    }

    if (fixfree) {
        printf("** Phase 6 - Salvage Free List\n");
        makefree();
        n_free = superblk.s_tfree;
    }

    // KBPB at the printf and nowhere else: n_blks and n_free are filesystem blocks up to
    // this line, which is ../README.md SS4's rule and ../df/README.md's account of why.
    printf("%d files %d blocks %d free\n", n_files, n_blks * KBPB, n_free * KBPB);

    // Both counters written again on the way out of a run that changed anything, without
    // asking, because any repair may have moved them: linkup() allocates a lost+found
    // entry, clri() frees an inode, and phase 6 rebuilds the free list under them.  This
    // run has just recomputed both, so there is no reason to leave a stale one behind.
    // (`modified' is bwrite()'s flag, so a run whose only repair was one of the two FIX
    // prompts above does not come through here -- the prompt has already set the field and
    // ckfini() has yet to flush it.)
    if (modified) {
        superblk.s_tinode = imax - n_files - 1; // the `- 1' is inode 1; see phase 4
        superblk.s_tfree  = n_free;
        superblk.s_time   = time((time_t *)0);
        sbdirty();
    }
    ckfini();
    sync();

    // v7's spin, kept.  The kernel's in-core superblock is now older than the disk's and
    // letting it be written back would undo the repair, so there is nothing this program
    // can do but refuse to return.  pause() rather than v7's `for(;;);': a busy loop here
    // spends a SIMH `step' budget that a test may be counting on.
    if (modified && hotroot) {
        printf("\n***** BOOT UNIX (NO SYNC!) *****\n");
        for (;;)
            pause();
    }
    if (modified)
        printf("\n***** FILE SYSTEM WAS MODIFIED *****\n");
}

//
// Open the device, read its superblock, and size the maps from it.
//
static int setup(char *dev)
{
    dev_t rootdev;
    struct stat statarea;

    if (stat("/", &statarea) < 0) {
        printf("Can't stat root\n");
        errexit();
    }
    rootdev = statarea.st_dev;

    if (stat(dev, &statarea) < 0) {
        printf("Can't stat %s\n", dev);
        return NO;
    }
    hotroot = 0;

    switch (statarea.st_mode & S_IFMT) {
    case S_IFBLK:
    case S_IFCHR:
        break;
    case S_IFREG:
        // A PLAIN FILE IS A FILESYSTEM IMAGE, and checking one is how this port tests
        // this program -- cmd/fsck/test/ runs it under b6sim, where the special is an
        // ordinary host file.  v7 asked "OK?" here, and reply() answers *no* whenever -n
        // is set, so the read-only case, which is the most useful one there is, could not
        // have been run at all.  fsck.1m.umm records the divergence.
        break;
    default:
        if (reply("file is not a block or character device; OK") == NO)
            return NO;
        break;
    }

    // AM I CHECKING THE FILESYSTEM I AM STANDING ON?  v7 asked ustat(2) and, failing
    // that, compared the root's st_dev with this device's st_rdev.  ustat(2) does not
    // exist here (the v7/x86 source had already stubbed it to -1), and the comparison
    // cannot work for the RAW node, which is the one fsck is normally pointed at:
    // rootdev is makedev(0,0) (kernel/conf.c) while /dev/rmd0's st_rdev is makedev(3,0).
    // So the raw name is mapped back to the block one, as 4.xBSD's unrawname() does.
    // The alternative would be to write cdevsw[]'s raw-to-block pairing into a user
    // program, which ../TODO.md forbids.
    if ((statarea.st_mode & S_IFMT) == S_IFBLK && statarea.st_rdev == rootdev)
        hotroot++;
    else if ((statarea.st_mode & S_IFMT) == S_IFCHR && israwroot(dev, rootdev))
        hotroot++;

    if ((rfd = open(dev, O_RDONLY)) < 0) {
        printf("Can't open %s\n", dev);
        return NO;
    }
    printf("\n%s", dev);
    if (nflag || (wfd = open(dev, O_WRONLY)) < 0) {
        wfd = -1;
        printf(" (NO WRITE)");
    }
    printf("\n");

    fixfree  = 0;
    modified = 0;
    n_files = n_blks = n_free = 0;
    muldup = enddup = &duplist[0];
    badlnp          = &badlncnt[0];
    lfdir           = 0;
    rplyflag        = 0;
    initbarea(&sblk);
    initbarea(&fileblk);
    initbarea(&inoblk);
    initbarea(&indblk);

    if (getblk(&sblk, SUPERB) == NULL) {
        ckfini();
        return NO;
    }
    // THE FOUR GEOMETRY WORDS FIRST, which v7's fsck does not look at -- it had none to
    // look at.  sbcheck() (kernel/alloc.c) refuses to mount a superblock that fails these
    // and cmd/fsutil/check.cpp refuses to check one, so a fsck that accepted it would be
    // the one program of the three with an opinion of its own.  That was a real
    // disagreement between this program and its oracle; ../fsck/README.md lists the rest.
    if (superblk.s_magic != FS_MAGIC) {
        printf("%s: not a filesystem\n", dev);
        ckfini();
        return NO;
    }
    if (superblk.s_bsize != BSIZEW || superblk.s_inopb != INOPB || superblk.s_naddr != NADDR) {
        printf("%s: filesystem geometry mismatch\n", dev);
        printf("bsize %d inopb %d naddr %d; this system wants %d %d %d\n", superblk.s_bsize,
               superblk.s_inopb, superblk.s_naddr, BSIZEW, INOPB, NADDR);
        ckfini();
        return NO;
    }

    imax = ((ino_t)superblk.s_isize - (SUPERB + 1)) * INOPB;
    fmin = (daddr_t)superblk.s_isize; // first data blk num
    fmax = superblk.s_fsize;          // first invalid blk num
    if (fmin >= fmax || (imax / INOPB) != ((ino_t)superblk.s_isize - (SUPERB + 1))) {
        printf("Size check: fsize %d isize %d\n", superblk.s_fsize, superblk.s_isize);
        ckfini();
        return NO;
    }

    // The maps, sized from the superblock rather than from an arena.  See the head
    // comment: this is what v7's scratch file, buffer pool and second code path were for,
    // and 2000 blocks with 1000 inodes needs about 1,300 words of them.
    bmapsz   = howmany(fmax, BITSPB);
    blkmap   = calloc((unsigned)bmapsz, 1);
    freemap  = calloc((unsigned)bmapsz, 1);
    statemap = calloc((unsigned)howmany(imax + 1, STATEPB), 1);
    lncntp   = calloc((unsigned)(imax + 1), sizeof(*lncntp));
    if (blkmap == NULL || freemap == NULL || statemap == NULL || lncntp == NULL) {
        printf("Can't get memory\n");
        ckfini();
        return NO;
    }
    return YES;
}

//
// Walk one inode's blocks: the six direct addresses, then NLEVEL of indirection.
//
static int ckinode(DINODE *dp, int flg)
{
    daddr_t iaddrs[NADDR];
    daddr_t *ap;
    int (*func)(daddr_t blk);
    int ret, n, i;

    if (SPECIAL)
        return KEEPON;

    // v7's l3tol(): the addresses were three packed bytes each there and are whole words
    // here, so this is a copy.  It is still a COPY and not a walk of dp->di_addr, because
    // descend() reloads inoblk through ginode() and dp would stop pointing at this inode.
    for (i = 0; i < NADDR; i++)
        iaddrs[i] = dp->di_addr[i];

    func = (flg == ADDR) ? pfunc : dirscan;
    for (ap = iaddrs; ap < &iaddrs[NADDR - NLEVEL]; ap++) {
        if (*ap && ((ret = (*func)(*ap)) & STOP))
            return ret;
    }
    for (n = 1; n <= NLEVEL; n++) {
        if (*ap && ((ret = iblock(*ap, n, flg)) & STOP))
            return ret;
        ap++;
    }
    return KEEPON;
}

//
// One indirect block, at level `ilevel'.
//
// THE BLOCK IS RE-FETCHED ON EVERY ITERATION and that is not redundant: the callback can
// descend into a subdirectory, which walks its own indirect blocks through this same
// buffer.  getblk() is free when the block is still there.  dirscan() below does the same
// thing for the same reason; v7 avoided it here with a 515-word automatic, which is not
// available to a buffer that must be MDALIGN-aligned.
//
static int iblock(daddr_t blk, int ilevel, int flg)
{
    int (*func)(daddr_t b);
    int i, n;
    daddr_t addr;

    if (flg == ADDR) {
        func = pfunc;
        if (((n = (*func)(blk)) & KEEPON) == 0)
            return n;
    } else
        func = dirscan;

    if (outrange(blk)) // protect thyself
        return SKIP;

    ilevel--;
    for (i = 0; i < NINDIR; i++) {
        if (getblk(&indblk, blk) == NULL)
            return SKIP;
        addr = ((daddr_t *)indblk.b_addr)[i];
        if (addr == 0)
            continue;
        n = (ilevel > 0) ? iblock(addr, ilevel, flg) : (*func)(addr);
        if (n & STOP)
            return n;
    }
    return KEEPON;
}

static int pass1(daddr_t blk)
{
    daddr_t *dlp;

    if (outrange(blk)) {
        blkerr("BAD", blk);
        if (++badblk >= MAXBAD) {
            printf("EXCESSIVE BAD BLKS I=%d", inum);
            if (reply("CONTINUE") == NO)
                errexit();
            return STOP;
        }
        return SKIP;
    }
    if (getbmap(blk)) {
        blkerr("DUP", blk);
        if (++dupblk >= MAXDUP) {
            printf("EXCESSIVE DUP BLKS I=%d", inum);
            if (reply("CONTINUE") == NO)
                errexit();
            return STOP;
        }
        if (enddup >= &duplist[DUPTBLSIZE]) {
            printf("DUP TABLE OVERFLOW.");
            if (reply("CONTINUE") == NO)
                errexit();
            return STOP;
        }
        for (dlp = duplist; dlp < muldup; dlp++) {
            if (*dlp == blk) {
                *enddup++ = blk;
                break;
            }
        }
        if (dlp >= muldup) {
            *enddup++ = *muldup;
            *muldup++ = blk;
        }
    } else {
        n_blks++;
        setbmap(blk);
    }
    filsize++;
    return KEEPON;
}

static int pass1b(daddr_t blk)
{
    daddr_t *dlp;

    if (outrange(blk))
        return SKIP;
    for (dlp = duplist; dlp < muldup; dlp++) {
        if (*dlp == blk) {
            blkerr("DUP", blk);
            *dlp    = *--muldup;
            *muldup = blk;
            return muldup == duplist ? STOP : KEEPON;
        }
    }
    return KEEPON;
}

static int pass2(DIRECT *dirp)
{
    int n, i;
    DINODE *dp;

    if ((inum = dirp->d_ino) == 0)
        return KEEPON;

    // The name, bounded.  v7 walked `p < &dirp->d_name[DIRSIZ]' and appended to pathname
    // with no bound at all.
    thisname = pathp;
    for (i = 0; i < DIRSIZ && PATHROOM > 0; i++) {
        if (dirp->d_name[i] == 0)
            break;
        *pathp++ = dirp->d_name[i];
    }
    *pathp = 0;

    n = NO;
    if (inum > imax || inum < ROOTINO)
        n = direrr("I OUT OF RANGE");
    else {
    again:
        switch (getstate()) {
        case USTATE:
            n = direrr("UNALLOCATED");
            break;
        case CLEAR:
            if ((n = direrr("DUP/BAD")) == YES)
                break;
            if ((dp = ginode()) == NULL)
                break;
            setstate(DIR ? DSTATE : FSTATE);
            goto again;
        case FSTATE:
            declncnt();
            break;
        case DSTATE:
            declncnt();
            descend();
            break;
        }
    }
    pathp  = thisname;
    *pathp = 0;
    if (n == NO)
        return KEEPON;
    dirp->d_ino = 0;
    return KEEPON | ALTERD;
}

static int pass4(daddr_t blk)
{
    daddr_t *dlp;

    if (outrange(blk))
        return SKIP;
    if (getbmap(blk)) {
        for (dlp = duplist; dlp < enddup; dlp++)
            if (*dlp == blk) {
                *dlp = *--enddup;
                return KEEPON;
            }
        clrbmap(blk);
        n_blks--;
    }
    return KEEPON;
}

static int pass5(daddr_t blk)
{
    if (outrange(blk)) {
        fixfree = 1;
        if (++badblk >= MAXBAD) {
            printf("EXCESSIVE BAD BLKS IN FREE LIST.");
            if (reply("CONTINUE") == NO)
                errexit();
            return STOP;
        }
        return SKIP;
    }
    if (getfmap(blk)) {
        fixfree = 1;
        if (++dupblk >= DUPTBLSIZE) {
            printf("EXCESSIVE DUP BLKS IN FREE LIST.");
            if (reply("CONTINUE") == NO)
                errexit();
            return STOP;
        }
    } else {
        n_free++;
        setfmap(blk);
    }
    return KEEPON;
}

// A block number is an on-disk quantity, not a measurement: no KBPB here.  SS4.
static void blkerr(const char *s, daddr_t blk)
{
    printf("%d %s I=%d\n", blk, s, inum);
    setstate(CLEAR); // mark for possible clearing
}

static void descend(void)
{
    DINODE *dp;
    char *savname;
    off_t savsize;

    setstate(FSTATE);
    if ((dp = ginode()) == NULL)
        return;
    savname = thisname;
    if (PATHROOM > 0)
        *pathp++ = '/';
    *pathp  = 0;
    savsize = filsize;
    filsize = dp->di_size;
    ckinode(dp, DATA);
    thisname = savname;
    // v7 wrote `*--pathp = 0' unconditionally, which walks off the front of the buffer if
    // the `/' above was not appended.  The guard is a SUBTRACTION and not `pathp >
    // pathname': that would be the very comparison SS2 forbids, on the two pointers this
    // function spends its life moving.
    if (pathp - pathname > 0)
        --pathp;
    *pathp  = 0;
    filsize = savsize;
}

//
// Every entry of one directory block.  The block is re-read each time round because the
// callback may descend and replace it, which is v7's own reason.
//
static int dirscan(daddr_t blk)
{
    int i, n;
    DIRECT direntry;

    if (outrange(blk)) {
        filsize -= BSIZE;
        return SKIP;
    }
    for (i = 0; i < DIRPB && filsize > 0; i++, filsize -= sizeof(DIRECT)) {
        if (getblk(&fileblk, blk) == NULL) {
            filsize -= (DIRPB - i) * sizeof(DIRECT);
            return SKIP;
        }
        // v7 copied the entry out and back byte by byte, backwards, through two
        // comparisons of char pointers (SS2).  An entry is four words and assigning one
        // is a word copy.
        direntry = dirblk[i];
        if ((n = (*dfunc)(&direntry)) & ALTERD) {
            if (getblk(&fileblk, blk) != NULL) {
                dirblk[i] = direntry;
                fbdirty();
            } else
                n &= ~ALTERD;
        }
        if (n & STOP)
            return n;
    }
    return filsize > 0 ? KEEPON : STOP;
}

static int direrr(const char *s)
{
    DINODE *dp;

    printf("%s ", s);
    pinode();
    if ((dp = ginode()) != NULL && ftypeok(dp))
        printf("\n%s=%s", DIR ? "DIR" : "FILE", pathname);
    else
        printf("\nNAME=%s", pathname);
    return reply("REMOVE");
}

static void adjust(int lcnt)
{
    DINODE *dp;

    if ((dp = ginode()) == NULL)
        return;
    if (dp->di_nlink == lcnt) {
        if (linkup() == NO)
            clri("UNREF", NO);
    } else {
        printf("LINK COUNT %s", (lfdir == inum) ? lfname : (DIR ? "DIR" : "FILE"));
        pinode();
        printf(" COUNT %d SHOULD BE %d", dp->di_nlink, dp->di_nlink - lcnt);
        if (reply("ADJUST") == YES) {
            dp->di_nlink -= lcnt;
            inodirty();
        }
    }
}

static void clri(const char *s, int flg)
{
    DINODE *dp;

    if ((dp = ginode()) == NULL)
        return;
    if (flg == YES) {
        printf("%s %s", s, DIR ? "DIR" : "FILE");
        pinode();
    }
    if (reply("CLEAR") == YES) {
        n_files--;
        pfunc = pass4;
        ckinode(dp, ADDR);
        zapino(dp);
        inodirty();
    }
}

//
// The inode `inum' is in, read one block at a time.  v7 had a second path here that swept
// the i-list NINOBLK blocks at a time into the arena; it is gone with the arena, and the
// alignment rules would have refused it in any case.
//
static DINODE *ginode(void)
{
    if (inum > imax)
        return NULL;
    if (getblk(&inoblk, itod(inum)) == NULL)
        return NULL;
    return inodes + itoo(inum);
}

static int ftypeok(DINODE *dp)
{
    switch (dp->di_mode & S_IFMT) {
    case S_IFDIR:
    case S_IFREG:
    case S_IFBLK:
    case S_IFCHR:
    case S_IFMPC:
    case S_IFMPB:
        return YES;
    default:
        return NO;
    }
}

static int reply(const char *s)
{
    char line[80];

    rplyflag = 1;
    printf("\n%s? ", s);
    if (nflag || csflag || wfd < 0) {
        printf(" no\n\n");
        return NO;
    }
    if (yflag) {
        printf(" yes\n\n");
        return YES;
    }
    // This libc's stdout is line buffered on a terminal where v7's was unbuffered, so a
    // prompt with no newline never arrives on its own.  cmd/login/README.md is the
    // account; login(1) needed the same call for the same reason.
    fflush(stdout);
    if (getline(stdin, line, sizeof(line)) == EOF)
        errexit();
    printf("\n");
    return (line[0] == 'y' || line[0] == 'Y') ? YES : NO;
}

static int getline(FILE *fp, char *loc, int maxlen)
{
    int n, i;

    i = 0;
    while ((n = getc(fp)) != '\n') {
        if (n == EOF)
            return EOF;
        // v7 compared two char pointers for the bound (SS2), and called isspace() on a
        // value that can be EOF -- which here would index a 256-entry table at -1.
        if (!isspace(n) && i < maxlen - 1)
            loc[i++] = n;
    }
    loc[i] = 0;
    return i;
}

//
// The three maps.  Each had a second arm in v7 reading the map out of a scratch file
// through the buffer pool; with the maps always in core, what is left is the arithmetic.
//
static int dostate(int s, int flg)
{
    char *p;
    int shift;

    p     = &statemap[inum / STATEPB];
    shift = LSTATE * (int)(inum % STATEPB);
    switch (flg) {
    case 0:
        *p &= ~(SMASK << shift);
        *p |= s << shift;
        return s;
    case 1:
        return (*p >> shift) & SMASK;
    }
    return USTATE;
}

static int domap(daddr_t blk, int flg)
{
    char *p;
    int n;

    // BITSPB is bits in a byte and BITSHIFT is its log: this shift is not the block-size
    // shift SS4 is about, and 8 really is a power of two.
    n = 1 << (int)(blk & BITMASK);
    p = ((flg & 04) ? freemap : blkmap) + (int)(blk >> BITSHIFT);
    switch (flg & 03) {
    case 0:
        *p |= n;
        break;
    case 1:
        n &= *p;
        break;
    case 2:
        *p &= ~n;
        break;
    }
    return n;
}

static int dolncnt(int val, int flg)
{
    int *sp;

    sp = &lncntp[inum];
    switch (flg) {
    case 0:
        *sp = val;
        break;
    case 2:
        (*sp)--;
        break;
    }
    return *sp;
}

static BUFAREA *getblk(BUFAREA *bp, daddr_t blk)
{
    if (bp->b_bno == blk)
        return bp;
    flush(bp);
    if (bread(bp->b_addr, blk) != NO) {
        bp->b_bno = blk;
        return bp;
    }
    bp->b_bno = (daddr_t)-1;
    return NULL;
}

static void flush(BUFAREA *bp)
{
    if (bp->b_dirty)
        bwrite(bp->b_addr, bp->b_bno);
    bp->b_dirty = 0;
}

static void rwerr(const char *s, daddr_t blk)
{
    printf("\nCAN NOT %s: BLK %d", s, blk);
    if (reply("CONTINUE") == NO) {
        printf("Program terminated\n");
        errexit();
    }
}

static void sizechk(DINODE *dp)
{
    if (DIR && (dp->di_size % sizeof(DIRECT)) != 0)
        printf("DIRECTORY MISALIGNED I=%d\n\n", inum);
}

static void ckfini(void)
{
    flush(&fileblk);
    flush(&sblk);
    flush(&inoblk);
    if (rfd >= 0)
        close(rfd);
    if (wfd >= 0)
        close(wfd);
    rfd = wfd = -1;

    // v7 kept its arena for the next filesystem; these are per-volume, being sized from
    // the superblock, so `fsck a b' has to give them back.
    free(blkmap);
    free(freemap);
    free(statemap);
    free(lncntp);
    blkmap = freemap = statemap = NULL;
    lncntp                      = NULL;
}

static void pinode(void)
{
    DINODE *dp;
    char *p;
    int i;
    // getpw(3) takes no length and writes a whole /etc/passwd line, so the bound is the
    // caller's and nothing else's.  256 rather than v7's 200 for the margin: the file is
    // the same one in both worlds -- the image's under SIMH, the same six lines compiled
    // into b6sim under b6sim (cmd/sim/etcfiles.cpp) -- but a line is only as long as
    // whoever last edited etc/passwd made it.
    char uidbuf[256];

    printf(" I=%d ", inum);
    if ((dp = ginode()) == NULL)
        return;
    printf(" OWNER=");
    if (getpw(dp->di_uid, uidbuf) == 0) {
        for (i = 0; uidbuf[i] != 0 && uidbuf[i] != ':'; i++)
            ;
        uidbuf[i] = 0;
        printf("%s ", uidbuf);
    } else
        printf("%d ", dp->di_uid);
    printf("MODE=%o\n", dp->di_mode);
    printf("SIZE=%d ", dp->di_size);
    p = ctime(&dp->di_mtime);
    printf("MTIME=%12.12s %4.4s ", p + 4, p + 20);
}

static void copy(char *fp, char *tp, int size)
{
    while (size--)
        *tp++ = *fp++;
}

static void clear(char *p, int cnt)
{
    while (cnt--)
        *p++ = 0;
}

//
// Walk the free list exactly as alloc() drains it, so a list this accepts is one the
// kernel can use.  df.c's alloc() is the same walk and cmd/fsutil/check.cpp's
// pass4_free_list() is the host's.
//
static void freechk(void)
{
    daddr_t *ap;

    if (freeblk.df_nfree == 0)
        return;
    do {
        if (freeblk.df_nfree <= 0 || freeblk.df_nfree > NICFREE) {
            printf("BAD FREEBLK COUNT\n");
            fixfree = 1;
            return;
        }
        ap = &freeblk.df_free[freeblk.df_nfree];
        while (--ap > &freeblk.df_free[0]) {
            if (pass5(*ap) == STOP)
                return;
        }
        if (*ap == (daddr_t)0 || pass5(*ap) != KEEPON)
            return;
    } while (getblk(&fileblk, *ap) != NULL);
}

//
// Phase 6.  v7 laid the new list out in the rotational pattern s_m/s_n described; this
// port has no such fields and no moving-head pack whose latency they were hiding, so the
// list is built plainly and descending -- which is exactly how ../mkfs/mkfs.c builds one,
// so a salvaged volume and a fresh one have the same shape.
//
static void makefree(void)
{
    daddr_t blk;
    int i;

    superblk.s_nfree  = 0;
    superblk.s_flock  = 0;
    superblk.s_fmod   = 0;
    superblk.s_tfree  = 0;
    superblk.s_ninode = 0;
    superblk.s_ilock  = 0;
    superblk.s_ronly  = 0;

    clear((char *)&freeblk, BSIZE);
    freeblk.df_nfree++; // slot 0 is the link to the next chain block

    for (blk = fmax - 1; blk >= fmin; blk--) {
        if (getbmap(blk))
            continue;
        superblk.s_tfree++;
        if (freeblk.df_nfree >= NICFREE) {
            fbdirty();
            fileblk.b_bno = blk;
            flush(&fileblk);
            clear((char *)&freeblk, BSIZE);
        }
        freeblk.df_free[freeblk.df_nfree] = blk;
        freeblk.df_nfree++;
    }
    superblk.s_nfree = freeblk.df_nfree;
    for (i = 0; i < NICFREE; i++)
        superblk.s_free[i] = freeblk.df_free[i];
    sbdirty();
}

static int findino(DIRECT *dirp)
{
    int i;

    if (dirp->d_ino == 0)
        return KEEPON;
    for (i = 0; i < DIRSIZ; i++) {
        if (srchname[i] != dirp->d_name[i])
            return KEEPON;
        if (dirp->d_name[i] == 0)
            break;
    }
    if (dirp->d_ino >= ROOTINO && dirp->d_ino <= imax)
        parentdir = dirp->d_ino;
    return STOP;
}

//
// Put the orphan in the first empty slot of lost+found, named for its i-number.
//
static int mkentry(DIRECT *dirp)
{
    ino_t in;
    int i;

    if (dirp->d_ino)
        return KEEPON;
    dirp->d_ino = orphan;
    in          = orphan;
    for (i = 0; i < DIRSIZ; i++)
        dirp->d_name[i] = 0;
    // Seven digits, filled from the right, as v7 did -- but derived rather than written
    // as `&dirp->d_name[7]', DIRSIZ being 18 here and 14 there.
    for (i = 7; --i >= 0;) {
        dirp->d_name[i] = (in % 10) + '0';
        in /= 10;
    }
    return ALTERD | STOP;
}

static int chgdd(DIRECT *dirp)
{
    if (dirp->d_name[0] == '.' && dirp->d_name[1] == '.' && dirp->d_name[2] == 0) {
        dirp->d_ino = lfdir;
        return ALTERD | STOP;
    }
    return KEEPON;
}

static int linkup(void)
{
    DINODE *dp;
    int lostdir;
    ino_t pdir;

    if ((dp = ginode()) == NULL)
        return NO;
    lostdir = DIR;
    pdir    = parentdir;
    printf("UNREF %s ", lostdir ? "DIR" : "FILE");
    pinode();
    if (reply("RECONNECT") == NO)
        return NO;

    orphan = inum;
    if (lfdir == 0) {
        inum = ROOTINO;
        if ((dp = ginode()) == NULL) {
            inum = orphan;
            return NO;
        }
        dfunc     = findino;
        srchname  = lfname;
        filsize   = dp->di_size;
        parentdir = 0;
        ckinode(dp, DATA);
        inum = orphan;
        if ((lfdir = parentdir) == 0) {
            printf("SORRY. NO lost+found DIRECTORY\n\n");
            return NO;
        }
    }
    inum = lfdir;
    if ((dp = ginode()) == NULL || !DIR || getstate() != FSTATE) {
        inum = orphan;
        printf("SORRY. NO lost+found DIRECTORY\n\n");
        return NO;
    }
    // Round the directory out to a whole block so that the zero entries past its size are
    // scanned: those are the empty slots.  A block holds DIRPB of them, so a lost+found
    // made by mkdir(1) and never used has DIRPB-2 -- which is why fsck.1m.umm's advice to
    // create and delete files in it first is marked as not true here.
    if (dp->di_size % BSIZE) {
        dp->di_size = roundup(dp->di_size, BSIZE);
        inodirty();
    }
    filsize = dp->di_size;
    inum    = orphan;
    dfunc   = mkentry;
    if ((ckinode(dp, DATA) & ALTERD) == 0) {
        printf("SORRY. NO SPACE IN lost+found DIRECTORY\n\n");
        return NO;
    }
    declncnt();
    if (lostdir) {
        dfunc = chgdd;
        dp    = ginode();
        if (dp != NULL) {
            filsize = dp->di_size;
            ckinode(dp, DATA);
        }
        inum = lfdir;
        if ((dp = ginode()) != NULL) {
            dp->di_nlink++;
            inodirty();
            setlncnt(getlncnt() + 1);
        }
        inum = orphan;
        printf("DIR I=%d CONNECTED. ", orphan);
        printf("PARENT WAS I=%d\n\n", pdir);
    }
    return YES;
}

//
// Is `dev' the raw name of the device the root is mounted on?  4.xBSD's unrawname(): drop
// the `r' from the last path component and ask about that name instead.  Every comparison
// here is by index; a path is walked, never ordered (../README.md SS2).
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

//
// One block, in and out.  Both obey the four conditions ../df/README.md lists: a whole
// BSIZE into a MDALIGN-aligned buffer at a block-aligned offset.  The multiply is a
// multiply -- v7's `blk<<BSHIFT' has no spelling here, 3072 not being a power of two.
//
static int bread(int *buf, daddr_t blk)
{
    if (lseek(rfd, (off_t)blk * BSIZE, SEEK_SET) < 0)
        rwerr("SEEK", blk);
    else if (read(rfd, (char *)buf, BSIZE) == BSIZE)
        return YES;
    rwerr("READ", blk);
    return NO;
}

static int bwrite(int *buf, daddr_t blk)
{
    if (wfd < 0)
        return NO;
    if (lseek(wfd, (off_t)blk * BSIZE, SEEK_SET) < 0)
        rwerr("SEEK", blk);
    else if (write(wfd, (char *)buf, BSIZE) == BSIZE) {
        modified = 1;
        return YES;
    }
    rwerr("WRITE", blk);
    return NO;
}

static void catch(int sig)
{
    (void)sig;
    ckfini();
    exit(4);
}

//
// v7's errexit() took a printf argument list; the caller prints for itself here, which
// leaves one variadic function's worth of code out of the program and reads the same.
// It does NOT flush: an interrupted repair leaves the disk as it was found.
//
static void errexit(void)
{
    exit(8);
}
