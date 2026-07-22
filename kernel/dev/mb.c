/*
 * BESM-6 magnetic drum driver (МБ) -- task 18b.3.
 *
 * The drums are the machine's paging store, and this kernel's swap device.  Two
 * controllers, 033 1 and 033 2, of 256 zones each; a zone is 8 service words plus 1 Kword
 * of data.  A zone is therefore two BSIZEW blocks, and the two drums together are ONE
 * LINEAR SPACE OF 1024 BLOCKS: the minor number selects nothing (mbopen() rejects any but
 * 0) and b_blkno alone says which drum, which zone and which part of it.  conf.c sizes
 * nswap to exactly that.  A filesystem could not live here, which is why the drums are a
 * major of their own with the disks (dev/md.c) on major 0 -- and why paging traffic does
 * not queue behind filesystem traffic, the two being independent channels.
 *
 * A TRANSFER IS ONE INSTRUCTION.  The control word names the memory page, the direction
 * and the zone all at once, and issuing it to 033 1 or 033 2 starts and finishes the
 * exchange; there is no command sequence, unlike the disk.  doc/Besm6_Peripherals.md has
 * the control word field by field, and doc/Intrinsics.md §6.3 is a drum page read already
 * written out in C.
 *
 * TWO TRANSFER SIZES, AND WHY BOTH ARE NEEDED.  CW_PAGE_MODE moves a whole 1024-word zone
 * in one exchange.  With it clear an exchange moves ONE 256-WORD SECTOR: DRUM_SECTOR (bits
 * 2-1) picks the sector within the zone and DRUM_PARAGRAF (bits 12-11) the quarter of the
 * memory page it lands in.  There is nothing in between -- the drum has no half-zone field,
 * the way the disk has DISK_HALFZONE -- so a 512-word block is either half of a page-mode
 * transfer or two sector transfers, and never one exchange of its own.
 *
 * Page mode alone would not do, because b_blkno cannot be assumed even.  Swap space is
 * handed out by malloc(swapmap, ...) in BLOCKS, and kernel/sys1.c allocates
 * (NCARGS + BSIZE - 1) / BSIZE of them for exec arguments -- nothing rounds that to a
 * zone, so every allocation after one odd-sized one starts mid-zone.  So mbstart() takes
 * page mode when the exchange happens to be zone-aligned, page-aligned in memory and a
 * whole zone long, and one sector otherwise; mbintr() chains the next chunk until the
 * request is done.  The common swap case -- a page-aligned coreaddr on an even block --
 * is one exchange; the awkward one still works, at four.
 *
 * COMPLETION is GRP_DRUM1_FREE or GRP_DRUM2_FREE in ГРП.  These are WIRED bits: they
 * cannot be dismissed with MOD_GRPCLR, and "free" means IDLE rather than "an exchange just
 * finished", so one stands whenever its drum is not transferring and must not sit armed in
 * МГРП outside an exchange (sys/besm6dev.h has the long version).  Hence the shape below:
 * mbstart() arms with mgrpon() AFTER issuing the control word -- issuing it is what lowers
 * the bit -- and mbintr() disarms with mgrpoff() before iodone(), but NOT when it chains,
 * because the next control word lowers the bit again by itself.  Task 18b.2 built that
 * pair; kernel/test/ugrp is what proves it.
 *
 * A MISSING DRUM DOES NOT HANG THE KERNEL.  An unattached unit makes 033 1 / 033 2 return
 * having transferred nothing and raised no interrupt, recording itself in the error mask at
 * EXT_IOERR instead.  mbstart() therefore polls that mask immediately after the exchange
 * instruction and fails the request rather than arming a completion bit that will never
 * come.  This is the drum's whole error story -- there is no status register and SIMH
 * models no parity, checksum or seek failures -- so unlike the disk, which has one and
 * classifies its failures through it (dev/md.c, task 18b.5), there is nothing here to retry
 * and nothing to ask.
 *
 * kernel/test/mbtest exercises all of it against SIMH.
 */

// clang-format off
#include "sys/types.h"
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/buf.h"
#include "sys/dir.h"
#include "sys/user.h"
#include "sys/besm6dev.h"
#include "sys/besm6disk.h"
// clang-format on

#include <besm6.h>

/*
 * The geometry, in the one unit the control word can address: the 256-word sector.
 */
#define MBSECT  256                         /* words in a sector -- the finest addressable unit */
#define MBSPP   (PGSZ / MBSECT)             /* 4 sectors to a zone */
#define MBSPB   (BSIZEW / MBSECT)           /* 2 sectors to a block */
#define MBNZONE 256                         /* zones on one drum */
#define MBNDRUM 2                           /* drums, i.e. controllers 033 1 and 033 2 */
#define MBNSECT (MBNDRUM * MBNZONE * MBSPP) /* 2048 sectors == 1024 blocks in all */

struct buf rmbbuf;
struct buf mbtab;

/*
 * The state of the one exchange that can be outstanding.  It belongs to the driver rather
 * than to the buf because there is never more than one: mbtab is a one-request-at-a-time
 * queue head, in the v7 way, and everything else waits on b_actf.
 */
static unsigned mbdone; /* words of mbtab.b_actf's request already transferred */
static unsigned mbnw;   /* words the exchange now running will move */
static unsigned mbarm;  /* the ГРП bit currently armed in МГРП, or 0 */

static void mbstart(void);

void mbopen(dev_t dev, int rw)
{
    /*
     * There is no unit to select.  Both drums are one block space and b_blkno picks
     * between them, so a minor number would only be a second, contradictory way to say
     * the same thing.
     */
    if (minor(dev) != 0)
        u.u_error = ENXIO;
}

void mbstrategy(register struct buf *bp)
{
    register struct buf *dp;
    unsigned sec;
    int s;

    /*
     * A drum address is a sector, so both ends of the transfer have to be sector-grained:
     * the control word has DRUM_PARAGRAF for the memory side and DRUM_SECTOR for the
     * device side, and neither has room for an offset within one.  swap() always obliges
     * -- it hands over a page-aligned coreaddr and a count clamped to a whole zone -- but
     * a raw physio() through /dev/mb need not, and an unaligned request has to be refused
     * rather than quietly landing 255 words away from where it was asked for.
     */
    if (bp->b_wcount == 0 || (bp->b_wcount & (MBSECT - 1)) != 0)
        goto bad;
    if ((bufpaddr(bp) & (MBSECT - 1)) != 0)
        goto bad;
    if (bp->b_blkno < 0)
        goto bad;
    sec = (unsigned)bp->b_blkno * MBSPB;
    if (sec + bp->b_wcount / MBSECT > MBNSECT)
        goto bad;

    bp->av_forw = NULL;
    s           = spl6();
    dp          = &mbtab;
    if (dp->b_actf == NULL)
        dp->b_actf = bp;
    else
        dp->b_actl->av_forw = bp;
    dp->b_actl = bp;
    if (dp->b_active == 0)
        mbstart();
    splx(s);
    return;

bad:
    bp->b_flags |= B_ERROR;
    bp->b_resid = bp->b_wcount;
    iodone(bp);
}

/*
 * Issue the next exchange of the request at the head of the queue, and keep issuing until
 * one of them actually starts.  Called with the queue non-empty and interrupts blocked --
 * from mbstrategy() at spl6, and from mbintr() with delivery held off by the gate.
 *
 * The loop is here rather than a tail call because a queue full of requests for a drum
 * nobody attached would otherwise recurse once per request.
 */
static void mbstart(void)
{
    register struct buf *bp;
    unsigned pa, sec, zone, ctlr, cw, bit;

    while ((bp = mbtab.b_actf) != NULL) {
        pa  = bufpaddr(bp) + mbdone;
        sec = (unsigned)bp->b_blkno * MBSPB + mbdone / MBSECT;

        /*
         * Unpack the linear sector number.  A drum is MBNZONE * MBSPP sectors, a zone is
         * MBSPP of them, and the zone number goes into DRUM_UNIT and DRUM_CYLINDER read
         * together: they are adjacent, and the pair IS the zone address, at bit 3.
         */
        ctlr = sec / (MBNZONE * MBSPP);
        zone = sec / MBSPP % MBNZONE;

        cw = cwpage(pa >> PGSH) | (zone << 2);
        if (bp->b_flags & B_READ)
            cw |= CW_READ;
        if (sec % MBSPP == 0 && (pa & (PGSZ - 1)) == 0 && bp->b_wcount - mbdone >= PGSZ) {
            /* Zone-aligned, page-aligned and a whole zone left: one exchange. */
            cw |= CW_PAGE_MODE;
            mbnw = PGSZ;
        } else {
            /* One sector, into the (pa >> 8 & 3)'th quarter of the memory page. */
            cw |= ((pa >> 8 & 3) << 10) | (sec % MBSPP);
            mbnw = MBSECT;
        }

        /*
         * A COMPUTED 033 address costs nothing here: the constant folds into the
         * instruction's own address field and only `ctlr' rides the C register --
         * `xta cw' / `wtc ctlr' / `ext EXT_DRUM1', three instructions, no worse than the
         * `if' over two constant addresses this used to be.
         *
         * WRITE THE VARIABLE FIRST.  `ctlr + EXT_DRUM1' folds; `EXT_DRUM1 + ctlr' does not
         * -- it puts the constant in the accumulator and the variable on the stack, which
         * is the one operand order the peephole cannot fuse, and costs a call to b$uadd
         * plus a stack round-trip.  Verified by disassembly.
         *
         * (b6cc once materialized a computed address into r14 and got it wrong, emitting
         * `14 ext 0' with a frame pointer still in r14, so the exchange went to whatever
         * device that address landed on.  That lowering is gone -- the address arrives
         * through the C register now, never an index register.  doc/Intrinsics.md §8.)
         */
        __besm6_ext(ctlr + EXT_DRUM1, cw);

        /*
         * Did the drum take it?  An unattached unit transfers nothing and interrupts
         * never, so this poll is the only thing between a missing drum and a kernel
         * waiting forever.  It has to come after the instruction: the mask is set by the
         * command that failed, and cleared by one that works.
         */
        if ((__besm6_ext(EXT_IOERR, 0) & IOERR_DRUM(ctlr)) == 0) {
            bit = GRP_DRUM1_FREE >> ctlr;
            if (mbarm != 0 && mbarm != bit)
                mgrpoff(mbarm); /* the request crossed from one drum to the other */
            mbarm = bit;
            mgrpon(bit);
            mbtab.b_active = 1;
            return;
        }

        /* No such drum.  Give up on this request and try the next one. */
        bp->b_flags |= B_ERROR;
        bp->b_resid  = bp->b_wcount - mbdone;
        mbdone       = 0;
        mbtab.b_actf = bp->av_forw;
        iodone(bp);
    }

    if (mbarm != 0) {
        mgrpoff(mbarm);
        mbarm = 0;
    }
    mbtab.b_active = 0;
}

/*
 * An exchange finished: GRP_DRUM1_FREE or GRP_DRUM2_FREE, from the ГРП dispatch in
 * kernel/intr.c.
 */
void mbintr(void)
{
    register struct buf *bp;

    /*
     * A completion nobody is waiting for.  It cannot happen from this driver -- only
     * mbstart() ever arms these bits, and only around a live exchange -- but the bit is
     * WIRED, so simply returning would leave it standing and extintr() would call us again
     * forever.  Disarming is the only way to make it stop, and it is what extintr()'s own
     * fallback probe would do for a bit with no handler.  kernel/test/mbtest check 5 forges
     * exactly this and holds the guard to account -- it is the half of kernel/test/ugrp that
     * moved here when these bits stopped being unhandled.
     */
    if (mbtab.b_active == 0) {
        mgrpoff(GRP_DRUM1_FREE | GRP_DRUM2_FREE);
        mbarm = 0;
        return;
    }

    bp = mbtab.b_actf;
    mbdone += mbnw;
    if (mbdone < bp->b_wcount) {
        /*
         * More to move.  Do NOT disarm: the control word mbstart() is about to issue
         * lowers the bit again by itself, which is exactly the arm/lower pairing the
         * wired bits require.
         */
        mbstart();
        return;
    }

    mbdone       = 0;
    bp->b_resid  = 0;
    mbtab.b_actf = bp->av_forw;
    iodone(bp);
    mbstart(); /* which disarms if the queue has run dry */
}

void mbread(dev_t dev)
{
    physio(mbstrategy, &rmbbuf, dev, B_READ);
}

void mbwrite(dev_t dev)
{
    physio(mbstrategy, &rmbbuf, dev, B_WRITE);
}
