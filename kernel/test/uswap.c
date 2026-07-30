// uswap -- a real process image through the real drum, and a shared text segment with it.
//
// Task 26's mechanical half, and the sixteenth standalone SIMH test.  Everything below the
// scheduler is REAL: kernel/text.o (xswap, xexpand, xccdec), kernel/slp.o (swapin, sleep,
// wakeup, swtch), kernel/dev/bio.o (swap), kernel/dev/mb.o (the drum) and kernel/intr.o
// (the ГРП dispatch that completes an exchange).  What is forged is only what a booting
// kernel would have supplied: proc[0], the coremap, the swapmap and a bdevsw with one row.
//
// WHAT THIS PROVES THAT NOTHING BEFORE IT DID.  kernel/test/usched runs the scheduler with
// swap() and xswap() STUBBED -- its comment says so -- and kernel/test/mbtest drives the
// drum with forged pages that no swapper ever asked for.  So until this test, no process
// image had ever been on the paging store: the two halves were each covered and the seam
// between them was not.  kernel/test/biotest covers swap()'s arithmetic against a recording
// strategy; here the words really go to a drum and really come back.
//
// IT RUNS UNDER `set mmu cache', but it does NOT cover kernel/dev/mb.c's drainbrz() -- that
// was measured rather than assumed, and uswap.ini says why at length.
//
// THE IMAGES LIVE IN LOW CORE, below the 32767 a C pointer can name (the second note in
// kernel/README.md's list for the next standalone test).  They are page-aligned words out of
// the real coremap, seeded from `end' upwards, so malloc() hands out addresses this file can
// both hand to the drum as a physical address and read back with an ordinary `int *'.  A
// swapper image above 0100000 is mbtest's and biotest's business; what is under test here is
// the content, and content has to be checkable.
//
// THE READ-BACK IS ONLY MEANINGFUL IF THE CORE WAS WIPED FIRST.  Every leg zeroes the whole
// free-core region between the write and the read, so a swap() that transferred nothing
// cannot pass by finding the pattern still sitting where it left it.

#include "sys/buf.h"
#include "sys/conf.h"
#include "sys/dir.h"
#include "sys/inode.h"
#include "sys/map.h"
#include "sys/param.h"
#include "sys/proc.h"
#include "sys/seg.h"
#include "sys/systm.h"
#include "sys/text.h"
#include "sys/types.h"
#include "sys/user.h"

void halt(int status);
struct trap;

// text.c keeps these two to itself -- no header declares them, because until now nothing
// outside that file had any business calling them.  This test does: they are the two ends
// of the shared-text store, and leg 4 drives them directly.
void xexpand(struct text *xp);
void xccdec(struct text *xp);

void intrinit(void);
void mbopen(dev_t, int);
void mbstrategy(struct buf *);
extern struct buf mbtab;

// b6ld defines it: the first word past bss.  The free core this test hands the coremap
// starts a page above, so the layout cannot silently collide with the image as the test
// grows -- which a hardcoded COREBASE would, and only by corrupting itself.
extern int end[];

// Failure bits, mirrored in uswap.ini's legend.
#define F_STUB   0000001 // a stub that must never run, ran
#define F_ERR    0000002 // a transfer reported B_ERROR (panic("IO err in swap") is F_STUB)
#define F_SWOUT  0000004 // xswap() did not leave the image on the paging store
#define F_SWIN   0000010 // swapin() refused, or did not take the core it was given
#define F_MOVED  0000020 // the image came back to the address it left from: proves nothing
#define F_DATA   0000040 // a swapped image came back with the wrong words
#define F_CROSS  0000100 // ... specifically, with the OTHER image's words
#define F_UFLUSH 0000200 // xswap() put a stale u page on the drum
#define F_UHOME  0000400 // uhome did not follow the image
#define F_TCC    0001000 // xccdec() wrote the text out too early, or not at all
#define F_TDATA  0002000 // the text segment came back from the drum wrong
#define F_TCNT   0004000 // x_ccount or x_flag came out wrong
#define F_COUNT  0010000 // the traffic counters do not match what was asked for
#define F_GROW   0020000 // the tail of an image grown by expand() did not come back zeroed

static int mask;

// -------------------------------------------------------------------------
// The environment the kernel objects name.
// -------------------------------------------------------------------------

struct proc proc[NPROC];
struct text text[NTEXT];
struct map coremap[CMAPSIZ];
struct map swapmap[SMAPSIZ];

int maxmem;
int mpid;
time_t time;
int lbolt;
int *intrframe; // extintr() dereferences it only on the timer arm

// One row: the drum.  swapdev is major 0 here, not conf.c's 1, because there is no disk
// on this machine and main()'s nblkdev walk is not running to be confused by a hole.
struct bdevsw bdevsw[] = {
    { mbopen, 0, mbstrategy, &mbtab },
    {},
};
int nblkdev   = 1;
dev_t swapdev = 0;
daddr_t swplo;
int nswap = 1024; // as kernel/conf.c: 2 drums * 256 zones * 2 blocks

// The head of bio.c's available list.  Nothing here goes through the buffer cache -- swap()
// has its own two bufs -- but binit() lives in kernel/main.c, which no test can link, so the
// cell has to come from somewhere.  Same reason as kernel/test/biotest.
struct buf bfreelist;

// kernel/subr.o's, defined here rather than linked for the reason biotest gives: subr.c
// would drag in half the kernel for one loop.
void wzero(void *dst, int nwords)
{
    register int *p = (int *)dst;

    while (nwords-- > 0)
        *p++ = 0;
}

// newproc() bumps u.u_cdir->i_count unconditionally; nothing here forks, but text.o's
// xfree()/xuntext() paths reference inodes and the linker wants the symbol resolved.
static struct inode fakecdir;

// -------------------------------------------------------------------------
// Stubs.  Each either cannot run here or does nothing that matters; the ones that must
// never run say so in the mask, so the test cannot quietly prove less than it claims.
// -------------------------------------------------------------------------

void panic(char *s)
{
    (void)s;
    mask |= F_STUB;
    halt(mask); // do not spin in idle(): a panic here means the test is void
}

void printf(char *fmt, ...)
{
    (void)fmt;
}

static int nclock; // free-running timer ticks; any number is fine

void clock(struct trap *tr)
{
    (void)tr;
    nclock++;
}

// No ПРП bit and no disk bit is ever armed here, so neither of these can fire.
void scintr(void)
{
    mask |= F_STUB;
}

void mdintr(void)
{
    mask |= F_STUB;
}

// text.o's inode half.  Nothing here execs, so xalloc() -- the only caller of readi() --
// is never reached, and xfree()/xuntext() are never asked to release an inode.
void readi(struct inode *ip)
{
    (void)ip;
    mask |= F_STUB;
}

void iput(struct inode *ip)
{
    (void)ip;
    mask |= F_STUB;
}

void psignal(struct proc *p, int sig)
{
    (void)p;
    (void)sig;
    mask |= F_STUB;
}

int issig(void)
{
    return 0;
}

// -------------------------------------------------------------------------
// Core layout, computed at run time from `end'.
// -------------------------------------------------------------------------

static int p0;       // proc[0]'s image: one page, its u-area home
static int corebase; // the free core handed to the coremap
static int coresize;

// The pattern a word of image `seed' at offset `i' must hold.  Position-dependent, so a
// transfer that moved the right number of words to the wrong place fails too.
static int pat(int seed, int i)
{
    return (seed << 20) + i + 1;
}

static void fill(int base, int nw, int seed)
{
    int *p = (int *)base;
    int i;

    for (i = 0; i < nw; i++)
        p[i] = pat(seed, i);
}

// Does [base, base+nw) hold image `seed'?  Returns 0 if it does; otherwise F_DATA, plus
// F_CROSS when the words are some OTHER image's -- which is what a wrong block number or a
// lost wtodb() looks like, and is worth telling apart from noise.
static int check(int base, int nw, int seed)
{
    int *p = (int *)base;
    int i, bad = 0, other = 0;

    for (i = 0; i < nw; i++) {
        if (p[i] == pat(seed, i))
            continue;
        bad = F_DATA;
        // pat() is invertible, so a wrong word can be asked WHOSE it is: undo the offset and
        // the shift, and a nonzero result that names another seed is another image's word --
        // which is what a wrong block number looks like, and worth telling from plain noise.
        if (p[i] != 0 && ((p[i] - i - 1) >> 20) != seed)
            other = F_CROSS;
    }
    return bad | other;
}

// Wipe every free page of core.  Between a swap-out and the swap-in that reads it back,
// this is what makes the read-back mean something: without it a swap() that did nothing at
// all would pass, the pattern still being where it was written.
static void wipe(void)
{
    int *p = (int *)corebase;
    int i;

    for (i = 0; i < coresize; i++)
        p[i] = 0;
}

// -------------------------------------------------------------------------
// The legs.
// -------------------------------------------------------------------------

// Give proc `pp' an image of `npg' pages at a fresh core address, filled with `seed'.
// Returns the address, or 0 if core ran out.
static int mkimage(struct proc *pp, int npg, int seed)
{
    int a = malloc(coremap, npg * PGSZ);

    if (a == NULL)
        return 0;
    fill(a, npg * PGSZ, seed);
    pp->p_addr  = a;
    pp->p_size  = npg * PGSZ;
    pp->p_stat  = SRUN;
    pp->p_flag  = SLOAD;
    pp->p_textp = NULL;
    pp->p_nice  = NZERO;
    pp->p_time  = 0;
    return a;
}

// Leg 0: the image that GREW before it was written -- expand()'s call, spelled out.
//
// kernel/slp.c's expand() raises p_size to the new size and THEN calls xswap(p, 1, n) with
// the OLD one, so the swap slot is allocated for the new size and only the old size is
// written; swapin() reads p_size back.  On the PDP-11 the tail came back as whatever the
// disk had there, which v7 did not care about -- getxfile() clearsegs the new image and
// grow() copies the stack into it.  On this machine it is not garbage, it is an ERROR: a
// drum zone the container file has never reached fails the read outright (SIMH's
// besm6_drum.c fails the short fread), kernel/dev/mb.c's EXT_IOERR poll cannot tell that
// from a missing drum, and kernel/dev/bio.c's swap() panics.
//
// THIS LEG RUNS FIRST, and that is not tidiness.  The swapmap is first-fit and the drum
// files are attached with `-n', i.e. empty: only a slot no previous leg has written lies
// past the end of the container, so a later leg would read back stale zeros and pass.  The
// bug is reachable exactly once per boot, at the first grown image -- which is also when a
// real kernel meets it.
static void leggrow(void)
{
    int a, n = 2 * PGSZ, grown = 5 * PGSZ, i;
    int *p;

    a = mkimage(&proc[3], 2, 4);
    if (a == 0) {
        mask |= F_SWIN;
        return;
    }
    proc[3].p_size = grown; // as expand() does, BEFORE the swap-out ...
    xswap(&proc[3], 1, n);  // ... and with the OLD size

    wipe();

    if (swapin(&proc[3]) == 0) {
        mask |= F_SWIN;
        return;
    }
    mask |= check(proc[3].p_addr, n, 4); // the half that was written
    p = (int *)(proc[3].p_addr + n);     // and the tail, which nobody wrote
    for (i = 0; i < grown - n; i++)
        if (p[i] != 0) {
            mask |= F_GROW;
            break;
        }

    mfree(coremap, grown, proc[3].p_addr);
    proc[3].p_stat = NULL;
}

// Leg 1 and leg 2 together: two images out to the drum, core wiped, and both read back --
// in the OTHER order, and each into core the other one's absence has moved.
//
// The order is the point.  A round trip that writes block b and reads block b back to the
// same place passes with any consistent addressing error, right or wrong; reading them back
// swapped means the two images' block numbers have to be right RELATIVE to each other as
// well as absolutely, and F_CROSS names the failure when they are not.
static void legimages(void)
{
    int a1, a2, n1 = 4, n2 = 3;
    int out1, out2;

    a1 = mkimage(&proc[1], n1, 1);
    a2 = mkimage(&proc[2], n2, 2);
    if (a1 == 0 || a2 == 0) {
        mask |= F_SWIN;
        return;
    }

    xswap(&proc[1], 1, 0); // ff = 1: the core goes back to the coremap
    xswap(&proc[2], 1, 0);
    out1 = proc[1].p_addr;
    out2 = proc[2].p_addr;

    // p_addr is a swap BLOCK now, not a core address, and SLOAD is clear.  The blocks come
    // out of swapmap, whose first block is 1, so a p_addr that still looked like core --
    // page-aligned and above corebase -- means xswap() never moved anything.
    if ((proc[1].p_flag & SLOAD) || (proc[2].p_flag & SLOAD) || out1 >= corebase ||
        out2 >= corebase || out1 == out2)
        mask |= F_SWOUT;

    wipe();

    // Read them back in the other order.  swapin() takes core from the coremap itself, and
    // because image 1 is the larger of the two the addresses cannot come out the same way
    // round they went in.
    if (swapin(&proc[2]) == 0 || swapin(&proc[1]) == 0) {
        mask |= F_SWIN;
        return;
    }
    if ((proc[1].p_flag & SLOAD) == 0 || (proc[2].p_flag & SLOAD) == 0)
        mask |= F_SWIN;
    if (proc[1].p_addr == a1 && proc[2].p_addr == a2)
        mask |= F_MOVED; // both landed where they started: the leg proves nothing

    mask |= check(proc[1].p_addr, n1 * PGSZ, 1);
    mask |= check(proc[2].p_addr, n2 * PGSZ, 2);

    mfree(coremap, proc[1].p_size, proc[1].p_addr);
    mfree(coremap, proc[2].p_size, proc[2].p_addr);
    proc[1].p_stat = NULL;
    proc[2].p_stat = NULL;
}

// Leg 3: the u-area invariant, on the drum.
//
// The live u-area is a fixed PHYSICAL page at UBASE and is NOT part of the image at p_addr;
// the copy sitting there is stale between context switches, and xswap() is one of the five
// places that has to say so (the block comment at xswap() in kernel/text.c is the whole
// rule).  Until now nothing had ever checked that from the far side: the invariant was
// tested by kernel/test/usched, where the "image" is core and the copy is a copyseg.
//
// So: poison the u page of proc[0]'s image with a STALE word, stamp FRESH into the LIVE
// u-area, and let xswap() write the image out.  What comes back off the drum must be FRESH.
//
// ff IS 0, deliberately.  With ff = 1 xswap() would free the core at p_addr and say
// NOUHOME, and this process -- proc[0], the one running -- would then have no home for the
// next resume() to flush to, which is exactly the state the real kernel only ever enters on
// its way out through qswtch().  newproc() uses ff = 0 for the same reason.
#define STALE 0525252
#define FRESH 0252525

static void leguarea(void)
{
    int blk, scratch;
    int *home = (int *)p0;
    // The sentinel's WORD OFFSET inside struct user, derived rather than spelled: `int' is
    // one word here, so the pointer difference is the offset uflush() will copy it to.
    int off = &u.u_arg[0] - (int *)&u;

    home[off]  = STALE; // the image's u page, as a context switch left it long ago
    u.u_arg[0] = FRESH; // the live u-area, as this process has it now

    xswap(&proc[0], 0, 0);
    blk = proc[0].p_addr;

    // Put proc[0] back together before anything can sleep: p_addr names a swap block right
    // now, and a resume() through it would load 1024 words of drum block into the u-area.
    proc[0].p_addr = p0;
    proc[0].p_flag |= SLOAD;

    if (uhome != p0)
        mask |= F_UHOME; // ff = 0 must leave the live u-area at home

    // uflush() must have run before the exchange, so the image in core now reads FRESH too.
    if (home[off] != FRESH)
        mask |= F_UFLUSH;

    // And what actually reached the drum.  A separate page of core, wiped first.
    scratch = malloc(coremap, USIZE);
    if (scratch == NULL) {
        mask |= F_SWIN;
        return;
    }
    fill(scratch, USIZE, 7);
    swap(blk, scratch, USIZE, B_READ);
    if (((int *)scratch)[off] != FRESH)
        mask |= F_UFLUSH;
    mfree(coremap, USIZE, scratch);
    mfree(swapmap, wtodb(USIZE), blk);
}

// Leg 4: a shared text segment, out to the drum and back.
//
// This is the half of task 26 that a booting kernel under load will NOT reliably reach.
// xccdec() writes a text out only when the LAST in-core sharer leaves and XWRIT is still
// set, and xexpand() reads one back only when a later sharer arrives to find x_ccount at
// zero -- so a load test proves the sharing and not the store behind it.  Here both are
// driven straight.
//
// The text is forged rather than exec'd: xalloc() is the only thing that builds one from an
// inode, and it needs readi() and the whole file system under it.  What is real is
// everything the swapper touches -- x_size, x_caddr, x_daddr, x_ccount, XWRIT -- which is
// what the counters and the drum see.
static void legtext(void)
{
    struct text *xp = &text[0];
    int ts          = 2 * PGSZ;
    int caddr, in0, out0;

    caddr = malloc(coremap, ts);
    if (caddr == NULL) {
        mask |= F_SWIN;
        return;
    }
    fill(caddr, ts, 3);

    xp->x_size   = ts;
    xp->x_caddr  = caddr;
    xp->x_daddr  = malloc(swapmap, wtodb(ts));
    xp->x_count  = 2;
    xp->x_ccount = 2;
    xp->x_flag   = XWRIT; // as xalloc() leaves a freshly loaded text
    xp->x_iptr   = NULL;
    if (xp->x_daddr == NULL) {
        mask |= F_SWIN;
        return;
    }

    out0 = ntextout;

    // The first sharer leaves.  Nothing may happen: the text is still in core for the other.
    xccdec(xp);
    if (xp->x_ccount != 1 || ntextout != out0 || xp->x_caddr != caddr || (xp->x_flag & XWRIT) == 0)
        mask |= F_TCC;

    // The last one leaves.  Now it is written out and the core is freed.
    xccdec(xp);
    if (xp->x_ccount != 0 || ntextout != out0 + 1 || (xp->x_flag & XWRIT))
        mask |= F_TCC;

    wipe();

    // A later sharer arrives and finds x_ccount zero, which is xalloc()'s xexpand() arm.
    // xexpand() is called with the text LOCKED and unlocks it itself.
    in0 = ntextin;
    xp->x_flag |= XLOCK;
    xexpand(xp);
    if (xp->x_ccount != 1 || ntextin != in0 + 1 || (xp->x_flag & XLOCK))
        mask |= F_TCNT;
    // No "did it move?" test here, unlike leg 1: malloc() is first-fit and the core the text
    // just gave back is the lowest free run, so it comes back to the very address it left --
    // and that is fine, because wipe() zeroed it in between.  The wipe is the guard.
    mask |= check(xp->x_caddr, ts, 3);

    mfree(coremap, ts, xp->x_caddr);
    mfree(swapmap, wtodb(ts), xp->x_daddr);
    xp->x_count  = 0;
    xp->x_ccount = 0;
    xp->x_flag   = 0;
}

// -------------------------------------------------------------------------

int main()
{
    int nout0, nin0;

    // Core: proc[0]'s u home first, then everything up to the u-area page.  Derived from
    // `end' so that growing this file cannot make the image and the pool overlap.
    p0       = pground((int)end + PGSZ);
    corebase = p0 + USIZE;
    coresize = (UBASE - corebase) & ~(PGSZ - 1);
    maxmem   = coresize;

    // proc[0], exactly as kernel/main.c sets it up: its image is the u-area page, and the
    // live u-area at UBASE is its own.
    proc[0].p_addr = p0;
    uhome          = p0;
    proc[0].p_size = USIZE;
    proc[0].p_stat = SRUN;
    proc[0].p_flag = SLOAD | SSYS;
    proc[0].p_nice = NZERO;
    u.u_procp      = &proc[0];
    u.u_cdir       = &fakecdir;
    u.u_rdir       = NULL;

    mfree(coremap, coresize, corebase);

    // The paging store, as kernel/machdep.c's startup() sets it up: blocks 1..nswap are
    // free and swplo then goes to -1, so swap block 1 is device block 0.
    swplo = 0;
    mfree(swapmap, nswap, 1);
    swplo--;

    // Open the door.  intrinit() before spl0(), as kernel/main.c does and for the reason
    // kernel/test/uclock spells out: with МГРП still empty a tick delivered in spl0()'s own
    // `vtm' has nothing to dismiss it.  From here on swap() may sleep, and the completion
    // that wakes it arrives through the real ГРП dispatch.
    intrinit();
    spl0();

    nout0 = nswapout;
    nin0  = nswapin;

    leggrow(); // FIRST: see the leg's own comment
    legimages();
    leguarea();
    legtext();

    // The counters the boot-level test asserts on have to be right here first: four
    // xswap()s and three swapin()s is what the legs above asked for, and a counter that
    // counted something else would make kernel/test/swap's evidence worthless.
    if (nswapout != nout0 + 4 || nswapin != nin0 + 3)
        mask |= F_COUNT;

    halt(mask);
    return 0;
}
