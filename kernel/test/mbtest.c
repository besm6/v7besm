/*
 * mbtest -- the drum driver moves data, both ways, in both transfer modes.  Task 18b.3.
 *
 * Links THE CODE UNDER TEST -- the kernel's own mb.o and intr.o -- and hand-builds only
 * the environment they name: iodone(), physio(), clock(), scintr() and the frame cell.
 * Everything below therefore goes the whole way round the real path,
 *
 *     mbstrategy() -> mbstart() -> 033 1 -> ГРП -> extintr() -> mbintr() -> iodone(),
 *
 * with no copy of anything in the middle.
 *
 * Modelled on ugrp, which is the closest idiom in this directory: no gate, no user mode,
 * no forged context.  Delivery is blocked with the real spl7() for the whole run and
 * extintr() is called by hand in a poll loop, so the interrupt arrives exactly where this
 * file says it does and not between two checks.  crt0.s's own 0501 stub is linked but is
 * never reached.
 *
 * TWO RUNS, chosen by the mode word at 0100 which mbtest.ini deposits after loading.
 * There are only two drums in the machine, so the missing-drum check cannot coexist with
 * a real second-drum transfer in one run:
 *
 *   mode 0 -- both drums attached.  Checks 1-3, ending with a round trip on DRUM 2, which
 *             is what proves the controller selection (EXT_DRUM1 + ctlr) and the bit
 *             selection (GRP_DRUM1_FREE >> ctlr) are not hard-wired to drum 1.
 *   mode 1 -- drum 2 detached.  Checks 1 and 2 again, then check 4: the same drum-2
 *             request must come back with B_ERROR and a full b_resid.  WITHOUT the
 *             EXT_IOERR poll in mbstart() this is a HANG, not a wrong answer -- an
 *             unattached unit transfers nothing and interrupts never -- so the failure
 *             mode here is `step' expiring, not a nonzero accumulator.
 *
 * WHY CHECK 1 COMES FIRST, and must: SIMH backs a drum with an ordinary file, and reading
 * a zone the file does not reach yet fails (drum_read() in besm6_drum.c raises the same
 * error bit an unattached drum does).  Check 1 writes zone 0 of drum 1 whole, which is
 * what makes every read after it legal.
 *
 * mbtest.ini asserts ACC == 0.  A nonzero ACC names the failing check -- see the F_* bits.
 */

// clang-format off
#include "sys/types.h"
#include "sys/param.h"
#include "sys/buf.h"
#include "sys/dir.h"
#include "sys/user.h"
#include "sys/besm6dev.h"
#include "sys/besm6disk.h"
// clang-format on

#include <besm6.h>

/* mb.c and intr.c, the code under test. */
void mbstrategy(struct buf *bp);
void extintr(void);
void intrinit(void);
void mgrpon(unsigned bits);
int spl7(void);
extern unsigned mgrp; /* intr.c's shadow of МГРП, which cannot be read back */

/*
 * увв 031 simulates a ГРП interrupt: GRP |= (ACC & 0xFFFFFF) << 24.  The deterministic
 * source uintr, uclock and ugrp forge bits with -- no device and no timing involved.
 */
#define EXT_GRPSET 031U

/*
 * Where the transfers go, and where the drum leaves its 8 service words.
 *
 * Physical page 020 -- word 040000 -- which is far above this image and page-aligned, as
 * the driver requires and swap() naturally provides.  It also has to be BELOW 32767: the
 * word field of a C pointer is 15 bits (ptrword() in sys/param.h), so a higher page could
 * not be reached from C at all.  That limit is the whole reason struct buf carries
 * b_paddr, and it does not constrain the driver -- b_paddr is a plain unsigned and reaches
 * all 512 Kwords -- only this test's ability to look at what arrived.
 */
#define MBPAGE  020
#define MBBUF   ((volatile unsigned *)(MBPAGE * PGSZ))
#define SYSDATA ((volatile unsigned *)SYSDATA_DRUM1)

/*
 * Which run this is.  Word 0100 is in the hole the const segment leaves between the
 * service-word buffers (010-067) and the interrupt vectors at 0500, so nothing in the
 * image occupies it and mbtest.ini can simply deposit into it.
 */
#define MODEWORD     (*(volatile unsigned *)0100)
#define MODE_BOTH    0 /* both drums attached */
#define MODE_MISSING 1 /* drum 2 detached */

/* Block 512 is zone 0 of the second drum: 256 zones * 2 blocks each. */
#define DRUM2BLK 512

/* An arbitrary recognizable pattern for the 8 service words. */
#define SYSPAT 0700000000000U

/* Fault-mask bits, returned in the accumulator.  Zero means every check passed. */
#define F_ERR    0000001 /* a transfer that should have worked reported B_ERROR */
#define F_PAGE   0000002 /* the page-mode round trip came back different */
#define F_SYSW   0000004 /* the service words did not survive the round trip */
#define F_SECT   0000010 /* the sector-mode round trip came back different */
#define F_DRUM2  0000020 /* the drum-2 round trip came back different */
#define F_NOERR  0000040 /* the missing drum did not report B_ERROR */
#define F_RESID  0000100 /* ...or did not leave the whole request in b_resid */
#define F_MODE   0000200 /* the mode word was neither 0 nor 1 */
#define F_DONE   0000400 /* a request came back with neither B_DONE nor a hang */
#define F_IDLE   0001000 /* a completion nobody was waiting for was not disarmed */
#define F_MAP    0002000 /* the sector write did not land where the block number says */
#define F_DRUM1X 0004000 /* ...and the drum-2 write landed on drum 1 */

/* ------------------------------------------------------------------------- */
/* The environment kernel/dev/mb.c and kernel/intr.c name.                    */
/* ------------------------------------------------------------------------- */

int *intrframe; /* extintr() dereferences it only on the timer arm */

/*
 * mbopen() is the only thing in mb.c that touches the u-area, and only to set u_error.
 * Ordinary bss will do, as in biotest -- there is no mapping here and nothing reads it.
 */
struct user u;

struct trap;

static int nclock; /* free-running timer ticks; any number is fine */

void clock(struct trap *tr)
{
    nclock++;
}

/* prpintr() calls this.  No ПРП bit is ever up here, so it must never run. */
void scintr(void)
{
    nclock--;
}

/*
 * extintr()'s disk arm.  md.o is not linked here -- this test is the drum's -- and no disk
 * bit is ever forged or armed, so like scintr() it must never run.  kernel/test/mdtest is
 * the mirror image: it links the real md.o and stubs mbintr() instead.
 */
void mdintr(void)
{
    nclock--;
}

/*
 * mbread()/mbwrite() name it.  Nothing here goes through the raw path -- physio() would
 * drag in the whole buffer layer and a u-area with a real u_offset -- so this is a stub
 * that must never run.
 */
void physio(void (*strat)(struct buf *), struct buf *bp, int dev, int rw)
{
    bp->b_flags |= B_ERROR;
}

/*
 * The bottom of the real bio.c, reduced to the one thing the driver depends on.  The poll
 * loop below waits for exactly this.
 */
void iodone(register struct buf *bp)
{
    bp->b_flags |= B_DONE;
}

/* ------------------------------------------------------------------------- */

static struct buf mbbuf;

/*
 * One request, start to finish.  Returns B_ERROR (0 when the transfer worked).
 *
 * B_PHYS with b_paddr is how swap() addresses a transfer, and the only form the drum
 * driver's alignment rules were written against; a plain b_un.b_addr would work too, this
 * page being below the 32767 a caddr_t can name, but it would not be what the real caller
 * does.
 */
static int xfer(daddr_t blk, unsigned nw, int rw)
{
    mbbuf.b_flags  = B_BUSY | B_PHYS | rw;
    mbbuf.b_dev    = 0;
    mbbuf.b_blkno  = blk;
    mbbuf.b_wcount = nw;
    mbbuf.b_paddr  = MBPAGE * PGSZ;
    mbbuf.b_resid  = 0;
    mbbuf.b_error  = 0;

    mbstrategy(&mbbuf);

    /*
     * SIMH does the transfer inside the 033 instruction but delays the completion bit by
     * 20 usec of model time, so this spins for a while.  A completion that never comes is
     * a hang, and mbtest.ini's `step' is what turns that into a failure.
     */
    while ((mbbuf.b_flags & B_DONE) == 0)
        extintr();

    return mbbuf.b_flags & B_ERROR;
}

static void fill(unsigned seed)
{
    int i;

    for (i = 0; i < PGSZ; i++)
        MBBUF[i] = seed + i;
}

static void clear(void)
{
    int i;

    for (i = 0; i < PGSZ; i++)
        MBBUF[i] = 0;
}

/*
 * The two halves of the page came from two different requests -- see check 2b.
 */
static int mapdiffers(unsigned seed0, unsigned seed1)
{
    int i;

    for (i = 0; i < BSIZEW; i++)
        if (MBBUF[i] != seed0 + i)
            return 1;
    for (i = 0; i < BSIZEW; i++)
        if (MBBUF[BSIZEW + i] != seed1 + i)
            return 1;
    return 0;
}

static int differs(unsigned seed)
{
    int i;

    for (i = 0; i < PGSZ; i++)
        if (MBBUF[i] != seed + i)
            return 1;
    return 0;
}

/*
 * Write a page, wipe core, read it back.  Returns the checks that failed.
 */
static unsigned roundtrip(daddr_t blk, unsigned seed, unsigned badbit)
{
    unsigned mask = 0;

    fill(seed);
    if (xfer(blk, PGSZ, B_WRITE))
        mask |= F_ERR;
    clear();
    if (xfer(blk, PGSZ, B_READ))
        mask |= F_ERR;
    if (differs(seed))
        mask |= badbit;
    return mask;
}

int main(void)
{
    unsigned mask = 0;
    int i;

    /*
     * Block delivery for the whole run.  extintr() is called by hand below and must be the
     * only thing servicing ГРП: an interrupt taken through crt0.s's gate would run the
     * drum's completion between two checks instead of inside xfer()'s loop.
     */
    spl7();
    intrinit();

    /*
     * ---- Check 1: page mode, and the service words -------------------------------
     *
     * Block 0, a whole page: zone-aligned, page-aligned in memory and a full zone long,
     * which is exactly the case mbstart() takes CW_PAGE_MODE for.  One exchange each way.
     *
     * The service words ride along.  A page write hands 010-017 to the drum and a page
     * read brings them back, so seeding them before the write and clobbering them after
     * proves the fixed low-memory buffer is real and that it is the drum filling it --
     * not something in this program that happens to have left the right values there.
     */
    for (i = 0; i < 8; i++)
        SYSDATA[i] = SYSPAT + i;

    fill(01000);
    if (xfer(0, PGSZ, B_WRITE))
        mask |= F_ERR;

    clear();
    for (i = 0; i < 8; i++)
        SYSDATA[i] = 0;

    if (xfer(0, PGSZ, B_READ))
        mask |= F_ERR;
    if (differs(01000))
        mask |= F_PAGE;
    for (i = 0; i < 8; i++)
        if (SYSDATA[i] != SYSPAT + i)
            mask |= F_SYSW;

    /*
     * ---- Check 2: sector mode ----------------------------------------------------
     *
     * Block 1 -- an ODD block, which is the case the whole sector path exists for, and
     * the one swap() produces as soon as one odd-sized swapmap allocation has been made.
     * A page starting there is sectors 2 and 3 of zone 0 followed by sectors 0 and 1 of
     * zone 1, so it is four exchanges each way, chained through mbintr(), spanning two
     * zones, and landing in all four quarters of the memory page in turn.  If
     * DRUM_PARAGRAF and DRUM_SECTOR were swapped, or either were built at the wrong
     * shift, this is the check that says so.
     */
    mask |= roundtrip(1, 02000, F_SECT);

    /*
     * ---- Check 2b: and it landed WHERE the block number says --------------------------
     *
     * Check 2 on its own cannot tell: a driver that ignored the odd block and moved the
     * whole of zone 0 in page mode would write its pattern and read the same pattern back,
     * and pass.  (Confirmed by making mbstart() take page mode unconditionally -- check 2
     * still passed.)  What pins it down is reading zone 0 back WHOLE and looking at both
     * halves, because the two writes so far must have left it half one pattern and half
     * the other: block 0's page write filled all four sectors with 01000+i, then block 1's
     * sector writes replaced sectors 2 and 3 -- the second BSIZEW of the zone -- with
     * 02000+i, and left sectors 0 and 1 alone.
     *
     * So this is the check that says b_blkno -> zone and sector is the identity the rest
     * of the kernel thinks it is, rather than merely self-consistent.
     */
    clear();
    if (xfer(0, PGSZ, B_READ))
        mask |= F_ERR;
    if (mapdiffers(01000, 02000))
        mask |= F_MAP;

    if (MODEWORD == MODE_BOTH) {
        /*
         * ---- Check 3: the second drum --------------------------------------------
         *
         * Block 512, zone 0 of drum 2.  The data proves the whole address decode again,
         * but the point is the two things that are per-controller: the instruction
         * address (EXT_DRUM1 + ctlr) and the ГРП bit (GRP_DRUM1_FREE >> ctlr).  Get the
         * second wrong and this hangs rather than fails.
         */
        mask |= roundtrip(DRUM2BLK, 03000, F_DRUM2);

        /*
         * And it went to the OTHER drum.  Same argument as check 2b: with ctlr stuck at 0,
         * block 512 would come out as sector 1024 of drum 1, whose zone field wraps in
         * eight bits straight back to zone 0 -- so the round trip above would still pass,
         * having quietly destroyed what checks 1 and 2 put there.  Zone 0 of drum 1 must
         * still read back exactly as it did a moment ago.
         */
        clear();
        if (xfer(0, PGSZ, B_READ))
            mask |= F_ERR;
        if (mapdiffers(01000, 02000))
            mask |= F_DRUM1X;
    } else if (MODEWORD == MODE_MISSING) {
        /*
         * ---- Check 4: a missing drum fails instead of hanging ---------------------
         *
         * The same request with drum 2 detached.  033 2 then transfers nothing, raises
         * no completion interrupt, and says so only in the error mask at EXT_IOERR --
         * which mbstart() polls immediately after the exchange instruction for exactly
         * this reason.  Reaching the assertions below at all is most of the check.
         */
        if (xfer(DRUM2BLK, PGSZ, B_READ) == 0)
            mask |= F_NOERR;
        if (mbbuf.b_resid != PGSZ)
            mask |= F_RESID;
    } else {
        mask |= F_MODE;
    }

    /*
     * ---- Check 5: a completion nobody is waiting for ---------------------------------
     *
     * Forge GRP_DRUM1_FREE with the drum idle, arm it, and call extintr().  It must
     * RETURN -- a failure here is a hang, which is what mbtest.ini's `step' catches --
     * and it must have done so by taking the bit out of МГРП, because taking it out of
     * ГРП is not possible: these are wired bits and MOD_GRPCLR does nothing to them.
     * mbintr()'s idle guard is the only thing that can end this loop.
     *
     * ugrp used to hold this ground with the same forged bit.  Task 18b.3 gave the bit a
     * handler, so ugrp moved on to go on testing extintr()'s own fallback probe -- first to
     * GRP_CHAN3_FREE and then, when 18b.4 claimed that one too, to a tape bit -- and the
     * drum half of it belongs here now, with the real mb.o linked, which ugrp does not have.
     * kernel/test/mdtest holds the same ground for the disk.
     */
    __besm6_ext(EXT_GRPSET, (unsigned)(GRP_DRUM1_FREE >> 24));
    mgrpon(GRP_DRUM1_FREE);

    extintr();

    if (mgrp & GRP_DRUM1_FREE)
        mask |= F_IDLE;

    if ((mbbuf.b_flags & B_DONE) == 0)
        mask |= F_DONE;

    return mask;
}
