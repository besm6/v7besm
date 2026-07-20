/*
 * BESM-6 magnetic disk driver (МД / КМД) -- tasks 18b.4 (transfers) and 18b.5 (failures).
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
 * anticipates it, but it is a filesystem-wide change and not this driver's.
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
 * on the drums, so a whole-drive filesystem is the honest arrangement.
 *
 * A MISSING DRIVE DOES NOT HANG THE KERNEL, the same lesson as the drum and for the same
 * reason: the track-address command to an unattached unit records itself in the error mask
 * at EXT_IOERR and returns WITHOUT scheduling a completion, so nothing ever interrupts.
 * mdstart() polls the mask after the exchange and fails the request instead of arming a bit
 * that will never come.
 *
 * ERRORS, AND THE ONE QUESTION THAT DECIDES HOW TO HANDLE THEM (task 18b.5).  The error
 * mask says only THAT something failed.  What the driver needs to know is whether the
 * exchange was refused before it began or attempted and abandoned partway, because that is
 * what decides whether an interrupt is still on its way:
 *
 *   Refused, no completion coming.  An unattached unit, and a WRITE to a read-only one.
 *   The request fails on the spot -- arming a completion bit here is exactly the hang the
 *   EXT_IOERR poll exists to prevent -- and retrying is wrong, not just useless.
 *
 *   Attempted and failed, completion still due.  A checksum or address-marker failure on
 *   the real machine; on SIMH, a read of a zone the backing file does not reach.  The
 *   interrupt arrives regardless, so the retry waits for it and happens in mdintr(), where
 *   the completion has been consumed and cannot race the re-issued exchange.
 *
 * The two are told apart by the STATUS REGISTER at 033 4003 / 033 4004, which is a latch
 * read in two halves -- and of which SIMH computes only two bits correctly.  The table at
 * MDST_READY below has that story, including why MDST_ABSENT must never be used as the
 * presence test it looks like.  Retries are counted in mdtab.b_errcnt, MDRETRY of them, and
 * a request that runs out fails with B_ERROR, the untransferred remainder in b_resid and a
 * deverror() line naming the status word -- the v7 arrangement.
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

/*
 * The status-register commands.  These are the only three that WRITE the register: it is a
 * latch, not a live sample, and a transfer never touches it (disk_event() raises the ГРП
 * bit and nothing else).  So the status has to be ASKED for, and what is read without
 * asking is whatever the last query left behind.
 *
 * None of the three touches the ГРП free bit -- they fall through disk_ctl()'s command
 * switch, unlike unit select, group select and the unknown-command arm, all of which raise
 * it.  That is what makes it safe to query from inside the error path of a live request.
 */
#define MDCMD_STATCLR 010 /* clear the status register */
#define MDCMD_STATLO  011 /* latch bits 1-12 */
#define MDCMD_STATHI  031 /* latch bits 13-24 -- RETURNED SHIFTED DOWN BY 12 */

/*
 * The status register, at the TRUE bit positions of the 24-bit register.
 *
 * The two queries return two halves and MDCMD_STATHI's arrives pre-shifted down by twelve,
 * so mdstat() puts it back where it belongs and everything below compares against these.
 * Reassembling is not cosmetic: unshifted, MDST_POWERUP >> 12 and MDST_READY are both 0400
 * and a test for one would silently answer for the other.
 *
 * WHICH OF THESE SIMH ACTUALLY COMPUTES -- and the answer is not what the hardware
 * documentation says, nor what doc/Besm6_Peripherals.md said before this driver was
 * written.  Only two are usable:
 *
 *   MDST_READY     from MDCMD_STATLO.  Correct: it tests UNIT_ATT.
 *   MDST_READONLY  from MDCMD_STATHI.  Correct: it tests UNIT_RO, on its own `if'.
 *
 *   MDST_ABSENT    ALWAYS SET, on every drive, attached or not.  besm6_disk.c tests
 *                  `md_unit[c->dev].flags & UNIT_DISABLE', and UNIT_DISABLE (02000) is
 *                  SIMH's "this unit CAN be disabled" capability bit -- every md_unit[]
 *                  entry declares it in its UDATA -- not UNIT_DIS (04000), "is disabled".
 *   MDST_POWERUP   NEVER SET: it sits on the `else if' that MDST_ABSENT just took.
 *
 * So a driver that read the documentation and used MDST_ABSENT as its presence test would
 * fail every request on a perfectly good disk.  This driver consults neither, and the
 * error mask at EXT_IOERR remains the only presence test worth making; MDST_READY only
 * corroborates it.  Bite-tested -- classify on MDST_ABSENT and kernel/test/mdtest run 1
 * fails on every drive.  Same shape of trap as the unit mask above: the comment describes
 * the intent, the code does something else, and only the code runs.
 *
 * Everything else here is declared by the simulator and set by nothing, exactly as on the
 * drum.  They are named because the retry path is written against them for the real
 * machine's sake, and a reader is owed the fact that those arms never execute under SIMH.
 */
#define MDST_SEEK       00000377 /* 8-1: "seek done", one bit per unit */
#define MDST_READY      00000400 /* 9: the selected unit is ready         -- SIMH sets this */
#define MDST_SEEK_FAIL  00001000 /* 10: head location unknown */
#define MDST_CHECKSUM   00002000 /* 11: bad checksum on read */
#define MDST_FAILURE    00004000 /* 12: failure -- OR of some of the upper bits */
#define MDST_MAYDAY     00010000 /* 13: unspecified failure */
#define MDST_NO_AMRK    00020000 /* 14: address marker not found in a revolution */
#define MDST_WRONG_CYL  00040000 /* 15: wrong address marker */
#define MDST_WRONG_ID   00100000 /* 16: bad track ID */
#define MDST_BAD_ACSUM  00200000 /* 17: bad checksum of the address marker */
#define MDST_UNFINISHED 00400000 /* 18: I/O not finished after a revolution */
#define MDST_TRK_PARITY 01000000 /* 19: track parity error in two-track I/O */
#define MDST_READONLY   02000000 /* 20: the unit is read-only           -- SIMH sets this */
#define MDST_POWERUP    04000000 /* 21: powered up -- DEAD, see above */
#define MDST_ABSENT    010000000 /* 22: not connected -- ALWAYS SET, see above */
#define MDST_BUF_ERR   020000000 /* 23: transfer buffer not ready */

/*
 * How many times one exchange is re-issued before the request is failed.  The v7 figure,
 * from rk.c and rp.c.
 */
#define MDRETRY 10

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
static unsigned mderr;  /* the running exchange already failed; its completion is still due */

/*
 * The status register as of the last failure, reassembled from its two halves.  Not static:
 * kernel/test/mdtest reads it to check that a write-protected drive was told apart from an
 * absent one, the way it already reaches into intr.c for mgrp.
 */
unsigned mdstatus;

/*
 * Exchanges re-issued since the driver last started a fresh request.  Diagnostic state --
 * "did that request come back cleanly, or did it come back after a fight" is worth having
 * when a drive starts to go -- and the one thing that makes the hard/soft split above
 * OBSERVABLE.  Without it nothing distinguishes a soft error from a hard one from outside:
 * both end in a failed request with the same b_resid, and a driver that retried nothing
 * would look identical.  kernel/test/mdtest run 4 asserts on it for exactly that reason.
 */
unsigned mdretries;

static void mdstart(void);

/*
 * Ask the controller for half of its status register.  `cmd' is MDCMD_STATLO or MDCMD_STATHI.
 *
 * ONLY MEANINGFUL WITH A UNIT SELECTED, and both traps here bite silently.  MDCMD_STATLO
 * with no selection answers ~0 -- every error bit at once -- and MDCMD_STATHI with no
 * selection does not answer at all, leaving the previous query's value in the latch.  The
 * caller is safe because mdstart() issues the group and unit selects before every exchange
 * and the selection outlives it; a query from anywhere else would need its own select first.
 *
 * Constant 033 addresses, branching on ctlr, for the reason mdstart() does the same: a
 * computed __besm6_ext() address is miscompiled by b6cc today (see mdstart() below).  The
 * COMMAND is an accumulator value, so it may be a variable.
 */
static unsigned mdstat(unsigned ctlr, unsigned cmd)
{
    if (ctlr == 0) {
        __besm6_ext(EXT_DISKCTL3, cmd);
        return __besm6_ext(EXT_DISKSTAT3, 0);
    }
    __besm6_ext(EXT_DISKCTL4, cmd);
    return __besm6_ext(EXT_DISKSTAT4, 0);
}

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
    if (dp->b_actf == NULL) {
        mdretries  = 0; /* the count belongs to one request; this one starts a fresh queue */
        dp->b_actf = bp;
    }
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
         * on.  Verified by disassembly when the drum driver was written; see dev/mb.c,
         * which carries the same warning, and doc/Intrinsics.md on the computed form.
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
        mderr = 0;
        if (__besm6_ext(EXT_IOERR, 0) & IOERR_DISK(ctlr)) {
            /*
             * It did not.  Ask why, and the answer decides something sharper than a retry
             * policy: WHETHER A COMPLETION IS STILL COMING.  The mask says only that
             * something went wrong, and the controller reaches it two quite different ways.
             *
             *   No completion.  An unattached unit, and a WRITE to a read-only one, are
             *   both refused outright -- disk_ctl() sets the mask and returns BEFORE
             *   arming the completion event.  Nothing was started and nothing will arrive,
             *   so arming the ГРП bit here would be the hang this poll exists to prevent.
             *   These are the hard errors, and retrying is not merely futile but wrong.
             *
             *   Completion still due.  A transfer that was attempted and failed partway --
             *   on this simulator, a read of a zone the backing file does not reach; on the
             *   real machine, a checksum or address-marker failure -- leaves the mask set
             *   but the event armed, so the interrupt still arrives.  Re-issuing the
             *   exchange NOW would leave that completion in flight against the new one, so
             *   the retry belongs in mdintr(), once it has been consumed.  mderr carries
             *   the fact across.
             *
             * MDST_READY is the presence test and MDST_READONLY the write-protect one
             * because they are the only two bits this simulator computes correctly; the
             * table above says why MDST_ABSENT and MDST_POWERUP cannot be used.
             */
            mdstatus = mdstat(ctlr, MDCMD_STATLO) & 07777;
            mdstatus |= (mdstat(ctlr, MDCMD_STATHI) & 07777) << 12;

            if ((mdstatus & MDST_READY) == 0 ||
                ((bp->b_flags & B_READ) == 0 && (mdstatus & MDST_READONLY) != 0)) {
                /* Hard.  Give up on this request and try the next one. */
                deverror(bp, mdstatus, mdtab.b_errcnt);
                bp->b_flags |= B_ERROR;
                bp->b_resid    = bp->b_wcount - mddone;
                mddone         = 0;
                mdtab.b_errcnt = 0;
                mdtab.b_actf   = bp->av_forw;
                iodone(bp);
                continue;
            }
            mderr = 1;
        }

        bit = GRP_CHAN3_FREE >> ctlr;
        if (mdarm != 0 && mdarm != bit)
            mgrpoff(mdarm); /* the request crossed from one controller to the other */
        mdarm = bit;
        mgrpon(bit);
        mdtab.b_active = 1;
        return;
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

    /*
     * The exchange this completion belongs to had already reported an error, and waiting for
     * the completion before reacting is the whole point of mderr: the transfer was attempted,
     * so the interrupt was always going to arrive, and re-issuing before consuming it would
     * have left two completions racing for one armed bit.
     *
     * Now it is safe to try again.  mddone is untouched, so mdstart() re-issues exactly the
     * chunk that failed, and it must NOT disarm first -- the control word it is about to
     * issue lowers the wired bit by itself, which is the same arm/lower pairing the chaining
     * path below relies on.
     *
     * NONE OF THIS RUNS UNDER SIMH IN THE ORDINARY WAY, and it should not be read as tested
     * merely because the driver is.  The simulator models no checksum, marker or parity
     * failure at all; the one route to a soft error is a read of a zone the backing file
     * does not reach, which is what kernel/test/mdtest's short-image run is for.  On the
     * real machine this is the arm that matters, and it is written for that machine.
     */
    if (mderr) {
        mderr = 0;
        if (++mdtab.b_errcnt <= MDRETRY) {
            mdretries++;
            mdstart();
            return;
        }
        deverror(bp, mdstatus, mdtab.b_errcnt);
        bp->b_flags |= B_ERROR;
        bp->b_resid    = bp->b_wcount - mddone;
        mddone         = 0;
        mdtab.b_errcnt = 0;
        mdtab.b_actf   = bp->av_forw;
        iodone(bp);
        mdstart();
        return;
    }

    mdtab.b_errcnt = 0; /* this chunk landed; the next one starts its own count */
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
