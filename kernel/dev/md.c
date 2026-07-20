/* V7/x86 source code: see www.nordier.com/v7x86 for details. */
/* Copyright (c) 2007 Robert Nordier.  All rights reserved. */

/*
 * BESM-6 magnetic disk driver (МД / КМД) -- task 18b.4.
 *
 * The disks carry the filesystems: this is major 0, the device rootdev and pipedev name.
 * Swap lives on the drums instead (dev/mb.c) so that paging and filesystem traffic do not
 * queue behind one another -- the two are separate channels and can transfer at the same
 * time.
 *
 * THE ONLY DEVICE IN THE MACHINE WITH A TWO-STEP PROTOCOL, and that is the whole of what
 * makes it harder than the drum.  Where mbstart() issues one 033 and the exchange is over,
 * here the control word only DESCRIBES the transfer and a later controller command performs
 * it.  The full sequence, and the order matters -- see "WHY THIS ORDER" below:
 *
 *   033 023  MDCMD_GROUP | group    select the линейка
 *   033 023  MDCMD_UNIT  | 1<<unit  select the drive within it
 *   033 3    control word           what to move and where in memory.  Nothing happens yet.
 *   033 023  MDCMD_TRACK | ...      the track address -- AND THIS IS WHAT TRANSFERS.
 *
 * (033 4 and 033 024 for the second controller.)
 *
 * WHY THIS ORDER, and not the "step 1 then step 2" the documentation suggests.  The ГРП
 * "channel free" bit is WIRED, so it must be armed in МГРП only around a live exchange
 * (sys/besm6dev.h has the long version).  The natural reading -- issue the control word,
 * then run the commands, then arm -- leaves the bit STANDING at the moment of the arm: the
 * group and unit select commands RAISE the free bit (`GRP |= c->mask_grp' in disk_ctl()),
 * and only the control word lowers it (`GRP &= ~c->mask_grp' in disk_io()).  The driver
 * would then take a completion for a transfer that had not finished, and the device's real
 * completion, arriving afterwards, would be free to land on the NEXT request.  Putting both
 * selects BEFORE the control word makes the control word the last thing to touch the bit,
 * and then this driver arms exactly where mbstart() does -- after the exchange, once the
 * error poll has said the exchange took.
 *
 * BE HONEST ABOUT WHAT THAT IS WORTH HERE: kernel/test/mdtest does NOT catch the wrong
 * order.  It was bite-tested -- the sequence was rewritten control-word-first and the test
 * still passed -- because SIMH performs the whole transfer synchronously inside the
 * track-address command, so a completion taken early still finds the data in memory.  The
 * order is kept because it is right on the real machine, where the transfer takes
 * milliseconds and an early completion is a torn buffer, and because it costs nothing.  It
 * is not a rule this test defends, and nobody should believe it does.
 *
 * Group select invalidates the unit selection inside the controller (it sets its device
 * number back to -1), so both selects are reissued on every exchange rather than cached.
 * Two extra 033s is cheaper than the state, and much cheaper than the bug.
 *
 * THE UNIT MASK IS NOT INVERTED, whatever the hardware documentation says.  Bits 1-8 are a
 * one-hot drive mask and bit N selects drive N-1: `1 << unit'.  doc/Besm6_Peripherals.md
 * said the opposite -- bit 8 -> unit 0 -- and so did this file before the driver was
 * written; both were reproducing a comment in besm6_disk.c that contradicts the code
 * directly beneath it, which tests the bits from 8 downwards assigning 7 downwards.  Fixed
 * in the doc too.
 *
 * THE DRIVE TYPE IS ЕС-5052 (7.25 Mb), the simulator's default, and this driver only works
 * on that type.  On the 29 Mb ЕС-5061 the controller ignores CW_PAGE_MODE and transfers a
 * whole 1024-word zone every time, so a 512-word block would splatter over the next buffer;
 * it also splits the zone number across two commands.  Making a filesystem block a whole
 * zone would suit that drive, and include/sys/param.h's own BSIZE comment ("6144 for besm")
 * anticipates it, but it is a filesystem-wide change and not this task's.  kernel/TODO.md
 * under 18b.4 records the reasoning.
 *
 * GEOMETRY.  A zone is 8 service words plus 1 Kword of data, and on this drive a zone
 * transfers either whole (CW_PAGE_MODE) or as one half-zone "track" of 512 words.  A
 * half-zone IS a BSIZEW block, which is the happy accident this driver is built on: every
 * filesystem block is one native exchange, at any block number, odd or even -- unlike the
 * drum, which has no half-zone field and has to split an odd block into four sectors.
 * Both modes are here anyway: a page-aligned, zone-aligned, whole-zone request (physio, and
 * mdtest's placement check) goes in one exchange instead of two.
 *
 * THE MINOR NUMBER IS A FLAT DRIVE INDEX, deliberately identical to the simulator's own
 * unit subscript so that a minor number and a SIMH unit name are the same number:
 *
 *      bits 7-6  unused
 *      bit  5    controller: 0 = 033 3 (MD0-MD3), 1 = 033 4 (MD4-MD7)
 *      bits 4-3  group (линейка) 0-3 -- which of the controller's four SIMH devices
 *      bits 2-0  drive 0-7 within the group
 *
 * There are no partitions.  One drive is 1000 zones = 2000 blocks, about 6 Mb, and swap is
 * on the drums, so a whole-drive filesystem is the honest arrangement; the x86 driver's
 * partition scheme (rootdev was minor 56, an MBR slot number) went with hd.c.
 *
 * A MISSING DRIVE DOES NOT HANG THE KERNEL, the same lesson as the drum and for the same
 * reason: the track-address command to an unattached unit records itself in the error mask
 * at EXT_IOERR and returns WITHOUT scheduling a completion, so nothing ever interrupts.
 * mdstart() polls the mask after the exchange and fails the request instead of arming a bit
 * that will never come.  Everything past that -- the status register at 033 4003, retries
 * through b_errcnt -- is task 18b.5.
 *
 * kernel/test/mdtest exercises all of it against SIMH.
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
 * The geometry, in the unit the track-address command can name: the 512-word half-zone.
 */
#define MDTRACK BSIZEW                /* words in a half-zone -- and in a block */
#define MDTPZ   (PGSZ / MDTRACK)      /* 2 half-zones to a zone */
#define MDNZONE 1000                  /* zones `attach -n' formats; the field holds 1024 */
#define MDNBLK  (MDNZONE * MDTPZ)     /* 2000 blocks on one drive */
#define MDNUNIT 64                    /* 2 controllers * 4 groups * 8 drives */

/*
 * The controller commands of 033 023 / 033 024.  These live here rather than in
 * sys/besm6disk.h because that header owns the mass-storage family's CONTROL WORD layout --
 * the accumulator of 033 3 -- and says in its own closing comment that this second protocol
 * layer belongs with the code that exercises it.
 *
 * The command is decoded by testing bits from the top down, so these are alternatives and
 * never OR'd together.
 */
#define MDCMD_GROUP 01400 /* bit 9, in the pattern (cmd & 01774) == 01400; | group 0-3 */
#define MDCMD_UNIT  02000 /* bit 11; | (1 << unit) -- bit N selects drive N-1 */
#define MDCMD_TRACK 04000 /* bit 12; | (zone << 1) | track -- THIS PERFORMS THE TRANSFER */

struct buf rmdbuf;
struct buf mdtab;

/*
 * The state of the one exchange that can be outstanding.  It belongs to the driver rather
 * than to the buf because there is never more than one: mdtab is a one-request-at-a-time
 * queue head, in the v7 way, and everything else waits on b_actf.
 */
static unsigned mddone; /* words of mdtab.b_actf's request already transferred */
static unsigned mdnw;   /* words the exchange now running will move */
static unsigned mdarm;  /* the ГРП bit currently armed in МГРП, or 0 */

static void mdstart(void);

void mdopen(dev_t dev, int rw)
{
    if ((unsigned)minor(dev) >= MDNUNIT)
        u.u_error = ENXIO;
}

void mdstrategy(register struct buf *bp)
{
    register struct buf *dp;
    int s;

    /*
     * Both ends of a transfer have to be half-zone-grained.  The device side is named by
     * the track-address command, whose finest unit is the half-zone; the memory side by
     * CW_PAGE and DISK_HALFPAGE together, whose finest unit is likewise 512 words.  Neither
     * has room for an offset within one.  The buffer cache always obliges -- buffers[] is
     * page-aligned and every buffer is BSIZEW long, so buffer i starts on a page or a
     * half-page boundary -- but a raw physio() through /dev/md need not, and an unaligned
     * request has to be refused rather than quietly landing 511 words from where it asked.
     */
    if (bp->b_wcount == 0 || (bp->b_wcount & (MDTRACK - 1)) != 0)
        goto bad;
    if ((bufpaddr(bp) & (MDTRACK - 1)) != 0)
        goto bad;
    if (bp->b_blkno < 0)
        goto bad;
    if ((unsigned)minor(bp->b_dev) >= MDNUNIT)
        goto bad;
    if ((unsigned)bp->b_blkno + bp->b_wcount / MDTRACK > MDNBLK)
        goto bad;

    bp->av_forw = NULL;
    s           = spl6();
    dp          = &mdtab;
    if (dp->b_actf == NULL)
        dp->b_actf = bp;
    else
        dp->b_actl->av_forw = bp;
    dp->b_actl = bp;
    if (dp->b_active == 0)
        mdstart();
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
 * from mdstrategy() at spl6, and from mdintr() with delivery held off by the gate.
 *
 * The loop is here rather than a tail call because a queue full of requests for a drive
 * nobody attached would otherwise recurse once per request.
 */
static void mdstart(void)
{
    register struct buf *bp;
    unsigned pa, blk, dev, ctlr, group, unit, zone, track, cw, cmd, bit;

    while ((bp = mdtab.b_actf) != NULL) {
        pa  = bufpaddr(bp) + mddone;
        blk = (unsigned)bp->b_blkno + mddone / MDTRACK;

        dev   = minor(bp->b_dev);
        ctlr  = dev >> 5;
        group = dev >> 3 & 3;
        unit  = dev & 7;

        zone  = blk / MDTPZ;
        track = blk % MDTPZ;

        cw = cwpage(pa >> PGSH);
        if (bp->b_flags & B_READ)
            cw |= CW_READ;
        if (track == 0 && (pa & (PGSZ - 1)) == 0 && bp->b_wcount - mddone >= PGSZ) {
            /* Zone-aligned, page-aligned and a whole zone left: one exchange. */
            cw |= CW_PAGE_MODE;
            mdnw = PGSZ;
        } else {
            /*
             * One half-zone.  DISK_HALFPAGE picks the half of the MEMORY page; the half of
             * the ZONE rides in bit 1 of the track-address command below, and NOT in
             * DISK_HALFZONE, which the controller does not read (sys/besm6disk.h).
             */
            if (pa & MDTRACK)
                cw |= DISK_HALFPAGE;
            mdnw = MDTRACK;
        }
        cmd = MDCMD_TRACK | (zone << 1) | track;

        /*
         * Four constant addresses, and not __besm6_ext(EXT_DISK3 + ctlr, cw).  A CONSTANT
         * address folds into the instruction's own address field, which is what makes the
         * intrinsic one inline instruction; a computed one is supposed to go through r14
         * instead, and b6cc gets that wrong today -- it emits `14 ext 0' while leaving a
         * frame pointer in r14, so the exchange goes to whatever device that address lands
         * on.  Verified by disassembly when the drum driver was written; see dev/mb.c and
         * kernel/TODO.md under 18b.3.
         *
         * The order is the one the header comment argues for: both selects (which RAISE the
         * free bit) before the control word (which lowers it), and the track address last.
         */
        if (ctlr == 0) {
            __besm6_ext(EXT_DISKCTL3, MDCMD_GROUP | group);
            __besm6_ext(EXT_DISKCTL3, MDCMD_UNIT | (1 << unit));
            __besm6_ext(EXT_DISK3, cw);
            __besm6_ext(EXT_DISKCTL3, cmd);
        } else {
            __besm6_ext(EXT_DISKCTL4, MDCMD_GROUP | group);
            __besm6_ext(EXT_DISKCTL4, MDCMD_UNIT | (1 << unit));
            __besm6_ext(EXT_DISK4, cw);
            __besm6_ext(EXT_DISKCTL4, cmd);
        }

        /*
         * Did the drive take it?  An unattached unit transfers nothing and interrupts
         * never, so this poll is the only thing between a missing disk and a kernel waiting
         * forever.  It has to come after the track-address command: that command is what
         * sets the mask, and the control word before it is what cleared it.
         */
        if ((__besm6_ext(EXT_IOERR, 0) & IOERR_DISK(ctlr)) == 0) {
            bit = GRP_CHAN3_FREE >> ctlr;
            if (mdarm != 0 && mdarm != bit)
                mgrpoff(mdarm); /* the request crossed from one controller to the other */
            mdarm = bit;
            mgrpon(bit);
            mdtab.b_active = 1;
            return;
        }

        /* No such drive.  Give up on this request and try the next one. */
        bp->b_flags |= B_ERROR;
        bp->b_resid  = bp->b_wcount - mddone;
        mddone       = 0;
        mdtab.b_actf = bp->av_forw;
        iodone(bp);
    }

    if (mdarm != 0) {
        mgrpoff(mdarm);
        mdarm = 0;
    }
    mdtab.b_active = 0;
}

/*
 * An exchange finished: GRP_CHAN3_FREE or GRP_CHAN4_FREE, from the ГРП dispatch in
 * kernel/intr.c.
 */
void mdintr(void)
{
    register struct buf *bp;

    /*
     * A completion nobody is waiting for.  It cannot happen from this driver -- only
     * mdstart() ever arms these bits, and only around a live exchange -- but the bit is
     * WIRED, so simply returning would leave it standing and extintr() would call us again
     * forever.  Disarming is the only way to make it stop, and it is what extintr()'s own
     * fallback probe would do for a bit with no handler.  kernel/test/mdtest forges exactly
     * this and holds the guard to account.
     */
    if (mdtab.b_active == 0) {
        mgrpoff(GRP_CHAN3_FREE | GRP_CHAN4_FREE);
        mdarm = 0;
        return;
    }

    bp = mdtab.b_actf;
    mddone += mdnw;
    if (mddone < bp->b_wcount) {
        /*
         * More to move.  Do NOT disarm: the control word mdstart() is about to issue lowers
         * the bit again by itself, which is exactly the arm/lower pairing the wired bits
         * require.  (The selects it issues first raise the bit, but it is still armed and
         * delivery is still blocked, and the control word puts it back down before either
         * changes -- which is the whole reason the selects come first.)
         */
        mdstart();
        return;
    }

    mddone       = 0;
    bp->b_resid  = 0;
    mdtab.b_actf = bp->av_forw;
    iodone(bp);
    mdstart(); /* which disarms if the queue has run dry */
}

void mdread(dev_t dev)
{
    physio(mdstrategy, &rmdbuf, dev, B_READ);
}

void mdwrite(dev_t dev)
{
    physio(mdstrategy, &rmdbuf, dev, B_WRITE);
}
