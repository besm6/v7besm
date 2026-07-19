/*
 * biotest -- the I/O addressing plumbing of task 18a (kernel/dev/bio.c, kernel/utab.c).
 *
 * A standalone SIMH program: crt0.s brings the machine up and calls main(), and we link
 * the REAL bio.o against a hand-built environment, the way mmutest links the real utab.o.
 * Everything runs in supervisor mode -- reset leaves РежЭ set -- so sureg() can program
 * the hardware with no kernel under it.
 *
 * What it is about.  A device transfer names a PHYSICAL WORD address, and the machine has
 * 512 Kwords; but a caddr_t is a fat pointer whose word field is 15 bits and cannot name
 * anything above 32767 (doc/Besm6_Data_Representation.md §7).  So struct buf carries
 * b_paddr for a B_PHYS request, and this test is what proves the two fillers -- swap() and
 * physio() -- put the right value there for a target ABOVE that ceiling, which is the case
 * the old code got silently wrong.
 *
 * The map it builds, all of it above physical page 32 so that every user page is out of a
 * fat pointer's reach:
 *
 *      virtual page   0  |  1   2 | 3 .. 27 | 28 29 30 31
 *      physical page  39 | 41  42 |   --    | 43 44 45 46
 *                    text|  data  |unmapped |    stack
 *
 * Note the deliberate gap between the text page (39) and the first data page (41): the
 * image is at 40 and its first page is the u-area, which is not in the map.  That makes
 * virtual pages 0 and 1 adjacent but PHYSICALLY discontiguous, which is what leg C uses to
 * test physrange()'s contiguity walk without hand-building a descriptor.
 *
 * Four pages of stack puts the last one at virtual page 31, so virtual word 32767 is the
 * last mapped word and the NPAGE * PGSZ ceiling is live -- leg B tests both sides of it.
 *
 * main() returns 0 on success; a nonzero return names the check that failed, and
 * biotest.ini asserts on it.
 */

// clang-format off
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/dir.h"
#include "sys/user.h"
#include "sys/buf.h"
#include "sys/conf.h"
#include "sys/proc.h"
#include "sys/text.h"
#include "sys/seg.h"
// clang-format on

/*
 * The kernel globals bio.o and utab.o refer to.  In the kernel `u' is an absolute symbol
 * at 076000 and maxmem is counted by startup(); here they are just storage.
 */
struct user u;
int maxmem = 512 * 1024; /* words: a fully populated machine */

static struct proc pr;
static struct text tx;

/* brz.s */
void drainbrz(void);

/*
 * crt0.s's 0501 vector calls this.  We never enable external interrupts, so it can never
 * run; it exists because the vector names it.
 */
void extintr(void)
{
}

#define TEXTPG  39 /* physical page of the shared text */
#define IMAGEPG 40 /* physical page of the process image (its u-area page) */

#define DATAPG (IMAGEPG + 1) /* the image is u, then data, then stack */
#define STKPG  (IMAGEPG + 3)

#define SWAPPG 60 /* a physical page for the swap leg, well above 32767 */

/*
 * What a strategy routine was handed.  bufpaddr() is recorded alongside b_paddr because it
 * is the call 18b's driver will actually make, and the two must agree for a B_PHYS request.
 */
struct rec {
    unsigned paddr;
    unsigned bufpa;
    unsigned wcount;
    daddr_t blkno;
    int flags;
};

static struct rec rec[8];
static int nrec;
static int slept; /* a stub that must never run: recstrategy completes synchronously */

static void recstrategy(struct buf *bp)
{
    if (nrec < 8) {
        rec[nrec].paddr  = bp->b_paddr;
        rec[nrec].bufpa  = bufpaddr(bp);
        rec[nrec].wcount = bp->b_wcount;
        rec[nrec].blkno  = bp->b_blkno;
        rec[nrec].flags  = bp->b_flags;
    }
    nrec++;
    bp->b_resid = 0;
    bp->b_error = 0;
    bp->b_flags |= B_DONE;
}

/*
 * The environment bio.o names.  Only d_strategy is ever reached.
 */
struct bdevsw bdevsw[] = {
    { 0, 0, recstrategy, 0 },
    {},
};
int nblkdev = 1;

struct buf bfreelist;
dev_t swapdev  = 0;
daddr_t swplo  = 0;

void sleep(caddr_t chan, int pri)
{
    slept = 1;
}
void wakeup(caddr_t chan)
{
}
int spl0(void)
{
    return (0);
}
int spl6(void)
{
    return (0);
}
void splx(int s)
{
}
void panic(char *s)
{
    for (;;)
        ;
}
void wzero(void *dst, unsigned nwords)
{
    register unsigned *p = (unsigned *)dst;

    while (nwords-- > 0)
        *p++ = 0;
}

/*
 * A char* into user virtual word `w', at byte offset k.  The compiler makes the cast a fat
 * pointer at byte #0 (offset field 5, the MSB byte) and `+ k' walks toward the LSB -- the
 * same construction exec/namei use (doc/Besm6_Data_Representation.md §7).
 */
#define UBYTE(w, k) ((caddr_t)(int *)(w) + (k))
#define UPTR(w)     ((caddr_t)(int *)(w))

/* physio() and bdevsw's d_strategy agree on void (*)(struct buf *), so no cast. */
#define PHYSIO(rw) physio(recstrategy, &pb, 0, (rw))

/* Set up a raw transfer of `nw' words from user virtual word `w'. */
static void setio(unsigned w, unsigned nw, off_t off)
{
    u.u_base   = UPTR(w);
    u.u_count  = wtob(nw);
    u.u_offset = off;
    u.u_error  = 0;
    nrec       = 0;
}

/*
 * The raw-I/O buffer header.  In bss, so b_flags starts clear: physio() waits on B_BUSY
 * before it does anything, and a stack temp full of garbage would hang there.
 */
static struct buf pb;

int main()
{
    tx.x_caddr = TEXTPG * PGSZ;
    tx.x_size  = PGSZ;

    pr.p_addr  = IMAGEPG * PGSZ;
    pr.p_size  = USIZE + 6 * PGSZ;
    pr.p_textp = &tx;

    u.u_procp = &pr;
    u.u_tsize = PGSZ;     /* one text page   -> virtual page 0      */
    u.u_dsize = 2 * PGSZ; /* two data pages  -> virtual pages 1, 2  */
    u.u_ssize = 4 * PGSZ; /* four stack pages-> virtual pages 28-31 */

    sureg();

    /* The map is what the header comment claims, and all of it is above 32767. */
    if (physaddr(0) != TEXTPG * PGSZ)
        return (1);
    if (physaddr(PGSZ) != DATAPG * PGSZ)
        return (2);
    if (physaddr(NPAGE * PGSZ - 1) != (STKPG + 3) * PGSZ + PGSZ - 1)
        return (3);

    /*
     * ---- Leg A: swap() ----------------------------------------------------------
     *
     * A physical word address that no caddr_t can name.  Ask for more than the one-zone
     * clamp so the while(count) loop issues TWO transfers: the second one is what catches
     * a coreaddr or blkno advanced in the wrong unit.
     */
    nrec = 0;
    swap(7, SWAPPG * PGSZ, PGSZ + 100, B_READ);

    if (nrec != 2)
        return (10);
    /*
     * THE POINT OF THIS LEG.  60 * 1024 = 61440 = 0170000.  Through a fat pointer that is
     * masked to 15 bits it would read back as 061440 & 077777 = 028672 -- wrong, and
     * nonzero, which is the silent corruption b_paddr exists to kill.
     */
    if (rec[0].paddr != SWAPPG * PGSZ)
        return (11);
    if (rec[0].bufpa != SWAPPG * PGSZ) /* bufpaddr() must agree: B_PHYS is set */
        return (12);
    if (!(rec[0].flags & B_PHYS))
        return (13);
    if (rec[0].wcount != PGSZ) /* clamped to one drum zone, and counted in WORDS */
        return (14);
    if (rec[0].blkno != 7)
        return (15);

    /* Second transfer: the tail, one clamp further into core and wtodb() further along. */
    if (rec[1].paddr != SWAPPG * PGSZ + PGSZ)
        return (16);
    if (rec[1].wcount != 100)
        return (17);
    if (rec[1].blkno != 7 + (daddr_t)wtodb(PGSZ))
        return (18);

    /*
     * ---- Leg B: physio() --------------------------------------------------------
     */

    /*
     * A legitimate transfer of both data pages.  This is the case the OLD code rejected:
     * it compared a page number against u_tsize in WORDS, so nb = 1 < 1024 sent every
     * data transfer to `bad'.
     */
    setio(PGSZ, 2 * PGSZ, 0);
    PHYSIO(B_READ);
    if (u.u_error != 0)
        return (20);
    if (nrec != 1)
        return (21);
    if (rec[0].paddr != DATAPG * PGSZ) /* 41 * 1024 = 41984, above 32767 */
        return (22);
    if (rec[0].bufpa != DATAPG * PGSZ)
        return (23);
    if (rec[0].wcount != 2 * PGSZ) /* WORDS, not bytes */
        return (24);
    if (!(rec[0].flags & B_PHYS))
        return (25);

    /* b_blkno comes from a BYTE offset over 512-WORD blocks: word 512 is block 1. */
    setio(PGSZ, PGSZ, wtob(BSIZEW));
    PHYSIO(B_READ);
    if (u.u_error != 0 || rec[0].blkno != 1)
        return (26);

    /*
     * The NPAGE * PGSZ ceiling, both sides.  Virtual page 31 is the last stack page, so a
     * transfer that ends exactly at 32768 is legal and one word more is not.
     *
     * The old code bounded this with a bare `1024' -- a page count from a machine whose
     * pages were not these.  Once base is masked to its 15 bits that test can never fire,
     * so the reject below is what proves the replacement was not cosmetic.
     */
    setio(31 * PGSZ, PGSZ, 0);
    PHYSIO(B_READ);
    if (u.u_error != 0)
        return (27);
    if (rec[0].paddr != (STKPG + 3) * PGSZ)
        return (28);

    setio(31 * PGSZ, PGSZ + 1, 0);
    PHYSIO(B_READ);
    if (u.u_error != EFAULT)
        return (29);
    if (nrec != 0) /* and it never reached the device */
        return (30);

    /* Into the text: useracc() makes no read/write distinction, so physio() must. */
    setio(0, PGSZ, 0);
    PHYSIO(B_READ);
    if (u.u_error != EFAULT || nrec != 0)
        return (31);

    /* Across the data/stack gap, into pages that are not mapped. */
    setio(2 * PGSZ, 2 * PGSZ, 0);
    PHYSIO(B_READ);
    if (u.u_error != EFAULT || nrec != 0)
        return (32);

    /* A zero count. */
    setio(PGSZ, 0, 0);
    PHYSIO(B_READ);
    if (u.u_error != EFAULT || nrec != 0)
        return (33);

    /*
     * A fat pointer that does not sit on a word boundary.  A device moves whole words to a
     * physical page; a transfer starting at byte #1 cannot be expressed at all.
     */
    setio(PGSZ, PGSZ, 0);
    u.u_base = UBYTE(PGSZ, 1);
    PHYSIO(B_READ);
    if (u.u_error != EFAULT || nrec != 0)
        return (34);

    /* ...and a byte count that is not a whole number of words. */
    setio(PGSZ, PGSZ, 0);
    u.u_count = wtob(PGSZ) - 1;
    PHYSIO(B_READ);
    if (u.u_error != EFAULT || nrec != 0)
        return (35);

    /* physio() hands the residual back in BYTES, whatever the device counted in words. */
    setio(PGSZ, PGSZ, 0);
    PHYSIO(B_READ);
    if (u.u_error != 0 || u.u_count != 0)
        return (36);

    /*
     * ---- Leg C: physrange() -----------------------------------------------------
     *
     * The contiguity walk, tested directly.  Virtual pages 1 and 2 are the two data pages
     * and are physically adjacent (41, 42); virtual pages 0 and 1 are the text page and
     * the first data page and are NOT (39, 41), because the image's u-area page sits
     * between them and is not in the map.  No hand-built descriptor is needed.
     */
    if (physrange(PGSZ, 2 * PGSZ) != DATAPG * PGSZ) /* two pages, contiguous */
        return (40);
    if (physrange(0, PGSZ) != TEXTPG * PGSZ) /* one page is trivially contiguous */
        return (41);
    if (physrange(0, 2 * PGSZ) != 0) /* text -> data: a real discontinuity */
        return (42);
    if (physrange(5 * PGSZ, PGSZ) != 0) /* not mapped at all */
        return (43);
    if (physrange(NPAGE * PGSZ - 1, 2) != 0) /* runs off the end of the map */
        return (44);
    if (physrange(PGSZ, 0) != 0) /* an empty range has no address */
        return (45);

    /* The four stack pages are one run: physrange must accept all of it. */
    if (physrange(USTKPAGE * PGSZ, 4 * PGSZ) != STKPG * PGSZ)
        return (46);

    /*
     * Nothing above should have blocked: recstrategy completes synchronously, so a
     * sleep() means a handshake went wrong rather than a value being off.
     */
    if (slept)
        return (50);

    return (0);
}
