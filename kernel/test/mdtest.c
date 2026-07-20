/*
 * mdtest -- the disk driver moves data, through its two-step protocol.  Task 18b.4.
 *
 * The mirror of mbtest, and built the same way: this links THE CODE UNDER TEST -- the
 * kernel's own md.o and intr.o -- and stubs only what they name.  No gate and no user mode.
 * Delivery is blocked for the whole run with the real spl7() and extintr() is called as an
 * ordinary C function in a poll loop, so every completion arrives inside the request that
 * caused it rather than between two checks.  The path exercised end to end is
 *
 *      mdstrategy() -> mdstart() -> 033 023, 033 3, 033 023 -> ГРП -> extintr()
 *                   -> mdintr() -> iodone()
 *
 * WHAT THE DISK ADDS OVER THE DRUM, and therefore what this test is really for:
 *
 *   - A FOUR-COMMAND SEQUENCE where the drum has one instruction.  Group select and unit
 *     select RAISE the controller's wired ГРП free bit and only the exchange control word
 *     lowers it, so the order mdstart() issues them in is load-bearing; get it wrong and a
 *     completion is taken for a transfer that has not started.  Every check here that
 *     completes at all is a check on that order.
 *   - A HALF-ZONE that is exactly a BSIZEW block, selected by bit 1 of the track-address
 *     command and NOT by DISK_HALFZONE in the control word (which the controller does not
 *     read).  Checks 1-3 are about that bit.
 *   - DISK_HALFPAGE, an independent choice of which half of the MEMORY page is used.  It
 *     is what makes the odd-numbered entries of buffers[] legal transfer targets, so it is
 *     not decoration -- check 4.
 *   - GROUPS AND TWO CONTROLLERS behind one major number: checks 5 and 6.
 *
 * A ROUND TRIP PROVES NOTHING ABOUT PLACEMENT.  Writing a pattern and reading it back
 * passes whether or not the data went where the block number says -- a driver that is
 * consistently wrong twice looks right.  mbtest learned this the hard way and check 3 is
 * the answer here: leave two different patterns on the device from two different requests
 * and then read the region back WHOLE, so what is being checked is where the boundary
 * between them fell.  Check 6b is the same idea across controllers.
 *
 * The disk images are created with `attach -n', which FORMATS them -- unlike the drum,
 * whose backing file starts empty and cannot be read before it is written.  So the checks
 * here are in no particular order for that reason, only for the reason each one states.
 *
 * mdtest.ini asserts ACC == 0.  A nonzero ACC names the failing check -- see the F_* bits.
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

/* md.c and intr.c, the code under test. */
void mdstrategy(struct buf *bp);
void extintr(void);
void intrinit(void);
void mgrpon(unsigned bits);
int spl7(void);
extern unsigned mgrp; /* intr.c's shadow of МГРП, which cannot be read back */

/*
 * увв 031 simulates a ГРП interrupt: GRP |= (ACC & 0xFFFFFF) << 24.  The deterministic
 * source uintr, uclock, ugrp and mbtest forge bits with -- no device, no timing.
 */
#define EXT_GRPSET 031U

/*
 * Where the transfers go, and where controller 3 leaves its service words.
 *
 * Physical page 020 -- word 040000 -- far above this image and page-aligned.  It also has
 * to be BELOW 32767: the word field of a C pointer is 15 bits (ptrword() in sys/param.h),
 * so a higher page could not be read from C at all.  That limit is the whole reason struct
 * buf carries b_paddr, and it constrains this test rather than the driver -- b_paddr is a
 * plain unsigned and reaches all 512 Kwords.
 */
#define MDPAGE  020
#define MDBASE  (MDPAGE * PGSZ)
#define MDBUF   ((volatile unsigned *)MDBASE)
#define SYSDATA ((volatile unsigned *)SYSDATA_DISK3)

/*
 * Which run this is.  Word 0100 is in the hole the const segment leaves between the
 * service-word buffers (010-067) and the interrupt vectors at 0500, so nothing in the image
 * occupies it and mdtest.ini can simply deposit into it.
 */
#define MODEWORD     (*(volatile unsigned *)0100)
#define MODE_ALL     0 /* every unit attached */
#define MODE_MISSING 1 /* md40 -- the controller-4 unit -- detached */

/*
 * The three drives, as minor numbers.  The minor IS the simulator's unit subscript
 * (bit 5 controller, bits 4-3 group, bits 2-0 drive), which is the point of the map: minor
 * 0 is md00, minor 010 is md10, minor 040 is md40.
 */
#define UNIT_MD00 000 /* controller 3, group 0, drive 0 */
#define UNIT_MD10 010 /* controller 3, group 1, drive 0 -- proves the group select */
#define UNIT_MD40 040 /* controller 4, group 0, drive 0 -- proves the second controller */

/* Patterns.  All distinct, and far enough apart that a wrong offset cannot alias. */
#define PAT_A 0100000U /* block 0 of md00 -- zone 0, track 0 */
#define PAT_B 0200000U /* block 1 of md00 -- zone 0, track 1 */
#define PAT_C 0300000U /* block 2 of md00, written from the upper half of the page */
#define PAT_D 0400000U /* block 0 of md10 */
#define PAT_E 0500000U /* block 0 of md40 */

/* An arbitrary recognizable pattern for the service words. */
#define SYSPAT 0700000000000U

/* Fault-mask bits, returned in the accumulator.  Zero means every check passed. */
#define F_ERR    0000001 /* a transfer that should have worked reported B_ERROR */
#define F_TRK0   0000002 /* the half-zone round trip on an even block came back different */
#define F_SYSW   0000004 /* the service words did not survive the round trip */
#define F_TRK1   0000010 /* ...on an ODD block */
#define F_MAP    0000020 /* reading the zone whole did not show both halves where written */
#define F_HALFP  0000040 /* DISK_HALFPAGE did not move the memory side */
#define F_GROUP  0000100 /* the group-1 drive round trip came back different */
#define F_CTLR4  0000200 /* the controller-4 round trip came back different */
#define F_MD00X  0000400 /* ...and it landed on controller 3 instead */
#define F_NOERR  0001000 /* the missing unit did not report B_ERROR */
#define F_RESID  0002000 /* ...or did not leave the whole request in b_resid */
#define F_MODE   0004000 /* the mode word was neither 0 nor 1 */
#define F_DONE   0010000 /* a request came back with neither B_DONE nor a hang */
#define F_IDLE   0020000 /* a completion nobody was waiting for was not disarmed */

/* ------------------------------------------------------------------------- */
/* The environment kernel/dev/md.c and kernel/intr.c name.                    */
/* ------------------------------------------------------------------------- */

int *intrframe; /* extintr() dereferences it only on the timer arm */

/*
 * mdopen() is the only thing in md.c that touches the u-area, and only to set u_error.
 * Ordinary bss will do, as in mbtest and biotest -- there is no mapping here.
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
 * extintr()'s drum arm.  mb.o is not linked here -- this test is the disk's -- and no drum
 * bit is ever forged or armed, so like scintr() it must never run.  mbtest is the mirror
 * image: it links the real mb.o and stubs mdintr() instead.
 */
void mbintr(void)
{
    nclock--;
}

/*
 * mdread()/mdwrite() name it.  Nothing here goes through the raw path -- physio() would
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

static struct buf mdbuf;
static unsigned mask;

/*
 * One request, start to finish.  Returns B_ERROR (0 when the transfer worked).
 *
 * `off' is the offset within the test page, in words, so that check 4 can put the memory
 * side on a half-page boundary the way an odd entry of buffers[] does.
 *
 * B_PHYS with b_paddr is how swap() and physio() address a transfer, and the only form the
 * driver's alignment rules were written against.
 */
static int xfer(int unit, daddr_t blk, unsigned off, unsigned nw, int rw)
{
    mdbuf.b_flags  = B_BUSY | B_PHYS | rw;
    mdbuf.b_dev    = unit;
    mdbuf.b_blkno  = blk;
    mdbuf.b_wcount = nw;
    mdbuf.b_paddr  = MDBASE + off;
    mdbuf.b_resid  = 0;
    mdbuf.b_error  = 0;

    mdstrategy(&mdbuf);

    /*
     * SIMH does the transfer inside the track-address command but delays the completion bit
     * by 20 usec of model time, so this spins for a while.  A completion that never comes
     * is a hang, and mdtest.ini's `step' is what turns that into a failure.
     */
    while ((mdbuf.b_flags & B_DONE) == 0)
        extintr();

    if ((mdbuf.b_flags & B_DONE) == 0)
        mask |= F_DONE;

    return mdbuf.b_flags & B_ERROR;
}

static void fillrange(unsigned off, unsigned nw, unsigned seed)
{
    unsigned i;

    for (i = 0; i < nw; i++)
        MDBUF[off + i] = seed + i;
}

static void clearall(void)
{
    unsigned i;

    for (i = 0; i < PGSZ; i++)
        MDBUF[i] = 0;
}

static int cmprange(unsigned off, unsigned nw, unsigned seed)
{
    unsigned i;

    for (i = 0; i < nw; i++)
        if (MDBUF[off + i] != seed + i)
            return 1;
    return 0;
}

/*
 * Write one half-zone block, wipe core, read it back.  The workhorse of the checks that are
 * only about "did this drive answer at all".
 */
static unsigned roundtrip(int unit, daddr_t blk, unsigned seed, unsigned badbit)
{
    unsigned m = 0;

    fillrange(0, BSIZEW, seed);
    if (xfer(unit, blk, 0, BSIZEW, B_WRITE))
        m |= F_ERR;
    clearall();
    if (xfer(unit, blk, 0, BSIZEW, B_READ))
        m |= F_ERR;
    if (cmprange(0, BSIZEW, seed))
        m |= badbit;
    return m;
}

int main(void)
{
    unsigned mode;
    int i;

    /*
     * Block delivery for the whole run.  extintr() is called by hand below and must be the
     * only thing servicing ГРП: an interrupt taken through crt0.s's gate would run a disk
     * completion between two checks instead of inside xfer()'s loop.
     */
    spl7();
    intrinit();

    mode = MODEWORD;
    if (mode != MODE_ALL && mode != MODE_MISSING)
        mask |= F_MODE;

    /*
     * ---- Check 1: an even block, and the service words --------------------------------
     *
     * Block 0 is zone 0 track 0 of md00, 512 words at the base of the page: the plain
     * half-zone case, DISK_HALFPAGE clear and the track bit clear.
     *
     * The service words ride along.  In half-zone mode the controller touches only FOUR of
     * the eight -- 030-033 for track 0, 034-037 for track 1 -- so seeding those four before
     * the write and clobbering them before the read proves the fixed low-memory buffer is
     * real, that it is the controller filling it, and that the driver picked track 0.
     */
    for (i = 0; i < 4; i++)
        SYSDATA[i] = SYSPAT + i;

    fillrange(0, BSIZEW, PAT_A);
    if (xfer(UNIT_MD00, 0, 0, BSIZEW, B_WRITE))
        mask |= F_ERR;

    clearall();
    for (i = 0; i < 4; i++)
        SYSDATA[i] = 0;

    if (xfer(UNIT_MD00, 0, 0, BSIZEW, B_READ))
        mask |= F_ERR;
    if (cmprange(0, BSIZEW, PAT_A))
        mask |= F_TRK0;
    for (i = 0; i < 4; i++)
        if (SYSDATA[i] != SYSPAT + i)
            mask |= F_SYSW;

    /*
     * ---- Check 2: an ODD block ---------------------------------------------------------
     *
     * Block 1 is zone 0 TRACK 1 -- the same zone as check 1, the other half.  This is the
     * half the drum cannot express in one exchange at all, and the bit that expresses it
     * here lives in the track-address command rather than in the control word, so a driver
     * that reached for DISK_HALFZONE instead fails here.
     *
     * Memory stays at the base of the page: the two halves of the transfer are chosen
     * independently, and pinning one while moving the other is what keeps them separable.
     */
    fillrange(0, BSIZEW, PAT_B);
    if (xfer(UNIT_MD00, 1, 0, BSIZEW, B_WRITE))
        mask |= F_ERR;
    clearall();
    if (xfer(UNIT_MD00, 1, 0, BSIZEW, B_READ))
        mask |= F_ERR;
    if (cmprange(0, BSIZEW, PAT_B))
        mask |= F_TRK1;

    /*
     * ---- Check 3: WHERE the two halves landed, and page mode ---------------------------
     *
     * The check the other two cannot make.  Zone 0 now holds check 1's pattern in its first
     * half and check 2's in its second, written by two separate requests; read the whole
     * zone back in one exchange and require exactly that layout.
     *
     * Block 0 for a whole page is zone-aligned, page-aligned and a full zone long, which is
     * the case mdstart() takes CW_PAGE_MODE for -- so this is also the only check that
     * exercises the page-mode path, and a driver stuck in either mode fails it.  A track bit
     * built at the wrong shift fails it too, the halves having been swapped.
     */
    clearall();
    if (xfer(UNIT_MD00, 0, 0, PGSZ, B_READ))
        mask |= F_ERR;
    if (cmprange(0, BSIZEW, PAT_A) || cmprange(BSIZEW, BSIZEW, PAT_B))
        mask |= F_MAP;

    /*
     * ---- Check 4: DISK_HALFPAGE ---------------------------------------------------------
     *
     * The memory side on a half-page boundary, which is what an ODD entry of buffers[] is:
     * buffers is page-aligned at 064000 and each buffer is BSIZEW long, so buffer 1 starts
     * half a page in.  Without this bit those five buffers could not be transfer targets.
     *
     * Write block 2 (zone 1, track 0 -- a fresh zone) from the UPPER half of the page,
     * which now holds PAT_C while the lower half still holds PAT_A from check 3.  Then read
     * it back to the page BASE.  If the driver dropped DISK_HALFPAGE on the write it would
     * have sent the lower half instead, and PAT_A would come back rather than PAT_C.
     */
    fillrange(BSIZEW, BSIZEW, PAT_C);
    if (xfer(UNIT_MD00, 2, BSIZEW, BSIZEW, B_WRITE))
        mask |= F_ERR;
    clearall();
    if (xfer(UNIT_MD00, 2, 0, BSIZEW, B_READ))
        mask |= F_ERR;
    if (cmprange(0, BSIZEW, PAT_C))
        mask |= F_HALFP;

    /*
     * ---- Check 5: another group ---------------------------------------------------------
     *
     * md10 is drive 0 of GROUP 1 on the same controller.  Reaching it needs the group-select
     * command to have gone out and to have named group 1; drop it and the controller answers
     * from group 0, i.e. from md00, whose block 0 holds PAT_A rather than PAT_D.
     */
    mask |= roundtrip(UNIT_MD10, 0, PAT_D, F_GROUP);

    if (mode == MODE_ALL) {
        /*
         * ---- Check 6: the second controller --------------------------------------------
         *
         * md40 is on 033 4 / 033 024, and its completion is GRP_CHAN4_FREE.  Get the
         * addresses wrong and the exchange goes to another device; get the ГРП bit wrong and
         * this HANGS rather than fails, which is what `step' in the .ini is for.
         */
        mask |= roundtrip(UNIT_MD40, 0, PAT_E, F_CTLR4);

        /*
         * 6b, the mbtest lesson applied across controllers: with `ctlr' stuck at 0 the whole
         * of check 6 would have run on md00 and passed, having quietly destroyed what check
         * 1 wrote.  So read md00 block 0 back and require PAT_A to still be there.
         */
        clearall();
        if (xfer(UNIT_MD00, 0, 0, BSIZEW, B_READ))
            mask |= F_ERR;
        if (cmprange(0, BSIZEW, PAT_A))
            mask |= F_MD00X;
    } else {
        /*
         * ---- Check 7: a missing drive fails, and does not hang --------------------------
         *
         * md40 is detached in this run.  The track-address command records the unit in the
         * error mask at 033 4035 and returns WITHOUT scheduling a completion, so nothing
         * ever interrupts: without mdstart()'s EXT_IOERR poll this is a kernel waiting
         * forever rather than a failed request.
         */
        fillrange(0, BSIZEW, PAT_E);
        if (xfer(UNIT_MD40, 0, 0, BSIZEW, B_WRITE) == 0)
            mask |= F_NOERR;
        if (mdbuf.b_resid != BSIZEW)
            mask |= F_RESID;
    }

    /*
     * ---- Check 8: a completion nobody is waiting for ------------------------------------
     *
     * Forge GRP_CHAN3_FREE with the driver idle and arm it.  extintr() must RETURN -- a
     * failure here is a hang, which is what the .ini's `step' catches -- and it must have
     * done so by taking the bit out of МГРП, because taking it out of ГРП is not possible:
     * these are wired bits and MOD_GRPCLR does nothing to them.  mdintr()'s idle guard is
     * the only thing that can end this loop.
     *
     * ugrp used to hold this ground with this very bit.  Task 18b.4 gave it a handler, so
     * ugrp moved on to a tape bit and the disk half of it belongs here, where the real md.o
     * is linked.
     */
    __besm6_ext(EXT_GRPSET, (unsigned)(GRP_CHAN3_FREE >> 24));
    mgrpon(GRP_CHAN3_FREE);

    extintr();

    if (mgrp & GRP_CHAN3_FREE)
        mask |= F_IDLE;

    return mask;
}
