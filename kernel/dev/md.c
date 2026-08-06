// BESM-6 magnetic disk driver (МД / КМД) -- tasks 18b.4 (transfers) and 18b.5 (failures).
//
// Major 0, the device rootdev and pipedev name.  Swap is on the drums (dev/mb.c), a
// separate channel, so paging and filesystem traffic do not queue behind one another.
//
// THE ONLY DEVICE WITH A TWO-STEP PROTOCOL: the control word only DESCRIBES the transfer,
// and a later controller command performs it.  (033 4 / 033 024 for the second controller.)
//
//   033 023  MDCMD_GROUP | group    select the линейка
//   033 023  MDCMD_UNIT  | 1<<unit  select the drive within it
//   033 3    control word           what to move and where.  Nothing happens yet.
//   033 023  MDCMD_TRACK | ...      the track address -- AND THIS IS WHAT TRANSFERS.
//
// WHY THIS ORDER, and not the documentation's "step 1 then step 2".  The ГРП free bit is
// WIRED, so it may be armed only around a live exchange (sys/besm6dev.h).  Both selects
// RAISE it and only the control word lowers it, so control-word-first would leave it
// standing at the moment of the arm and the driver would take a completion for a transfer
// still running.  This way the control word is the last thing to touch the bit, and the arm
// happens after the exchange as in mbstart().  mdtest does NOT catch the wrong order --
// bite-tested; SIMH transfers synchronously inside the track-address command, so an early
// completion still finds the data.  The order is for the real machine.
//
// Group select invalidates the unit selection inside the controller, so both selects go out
// every exchange rather than being cached.
//
// THE UNIT MASK IS NOT INVERTED, whatever the hardware documentation says: bits 1-8 are a
// one-hot mask and bit N selects drive N-1, `1 << unit'.  doc/Besm6_Peripherals.md said the
// opposite, copying a comment in besm6_disk.c that its own code contradicts; fixed there too.
//
// THE DRIVE TYPE IS ЕС-5052 (7.25 Mb), the simulator's default, and only that type works
// here: the 29 Mb ЕС-5061 ignores CW_PAGE_MODE and always moves a whole zone, so a 512-word
// block would splatter over the next buffer.
//
// GEOMETRY.  A zone is 8 service words plus 1 Kword, transferred whole (CW_PAGE_MODE) or as
// one half-zone "track" of 512 words.  A half-zone IS a BSIZEW block: every filesystem block
// is one native exchange, odd or even -- unlike the drum, which splits an odd block into
// four sectors.  A page-aligned whole-zone request goes in one exchange instead of two.
//
// THE MINOR NUMBER IS A FLAT DRIVE INDEX, identical to the simulator's unit subscript:
// bit 5 the controller (0 = 033 3 / MD0-MD3, 1 = 033 4 / MD4-MD7), bits 4-3 the group
// (линейка), bits 2-0 the drive.  No partitions -- one drive is 1000 zones = 2000 blocks.
//
// A MISSING DRIVE DOES NOT HANG THE KERNEL: the track-address command to an unattached unit
// records itself in the error mask at EXT_IOERR and returns WITHOUT scheduling a completion,
// so mdstart() polls the mask and fails the request rather than arming a bit that never comes.
//
// ERRORS, AND THE QUESTION THAT DECIDES HOW TO HANDLE THEM (task 18b.5): was the exchange
// refused before it began, or attempted and abandoned?  That is what says whether an
// interrupt is still on its way.
//
//   Refused, no completion coming.  An unattached unit, and a WRITE to a read-only one.
//   Failed on the spot -- arming here is the hang the poll exists to prevent -- and
//   retrying is wrong, not merely useless.
//
//   Attempted and failed, completion still due.  A checksum or marker failure on the real
//   machine; on SIMH, a read past the end of the backing file.  The retry therefore waits
//   for the completion and happens in mdintr(), where it cannot race the re-issue.
//
// The two are told apart by the STATUS REGISTER at 033 4003 / 033 4004, of which SIMH
// computes only two bits correctly -- the table at MDST_READY below.  Retries are counted in
// mdtab.b_errcnt, MDRETRY of them; a request that runs out fails with B_ERROR, the remainder
// in b_resid and a deverror() line.
//
// THE INSTRUMENTATION IS THIS DRIVER'S, slot DK_MD (<sys/param.h>): the busy bit tracks
// mdtab.b_active, so it stands across a chained request and a retry, and dk_numb/dk_wds are
// bumped in mdintr() where a chunk landed rather than in mdstart() where one was issued.
// ONE SLOT FOR ALL 64 UNITS -- clock.c's busy set is a three-bit field.
//
// kernel/test/mdtest exercises all of it against SIMH, the counters included.

#include <besm6.h>

#include "sys/besm6dev.h"
#include "sys/besm6disk.h"
#include "sys/buf.h"
#include "sys/dir.h"
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/types.h"
#include "sys/user.h"

void drainbrz(void); // brz.s -- the nine stores that flush the write cache

// The geometry, in the unit the track-address command can name: the 512-word half-zone.
#define MDTRACK BSIZEW            // words in a half-zone -- and in a block
#define MDTPZ   (PGSZ / MDTRACK)  // 2 half-zones to a zone
#define MDNZONE 1000              // zones `attach -n' formats; the field holds 1024
#define MDNBLK  (MDNZONE * MDTPZ) // 2000 blocks on one drive
#define MDNUNIT 64                // 2 controllers * 4 groups * 8 drives

// The service-word buffer, at a FIXED PHYSICAL ADDRESS the hardware wires in: 030-037 for
// controller 3 and 040-047 for controller 4 (doc/Besm6_Peripherals.md).  The kernel runs
// unmapped, so this `int *' is a bare word address and the cast means what it says.  It is a
// driver register, not memory anyone else may use -- which is why sys/param.h's memory map
// has nothing below the kernel image.
#define MDSYS      ((int *)030) // controller 3's buffer; controller 4's is 8 words on
#define MDSYSWORDS 8            // service words to a zone
#define MDSYSHALF  4            // ... of which a half-zone transfer moves four
#define MDSYS_ADDR 36           // the sector address sits in bits 48-37 of the first word

// The controller commands of 033 023 / 033 024.  Here rather than in sys/besm6disk.h, which
// owns the CONTROL WORD layout and says this second protocol layer belongs with its code.
// Decoded by testing bits from the top down, so these are alternatives, never OR'd together.
#define MDCMD_GROUP 01400 // bit 9, in the pattern (cmd & 01774) == 01400; | group 0-3
#define MDCMD_UNIT  02000 // bit 11; | (1 << unit) -- bit N selects drive N-1
#define MDCMD_TRACK 04000 // bit 12; | (zone << 1) | track -- THIS PERFORMS THE TRANSFER

// The status-register commands, the only three that WRITE the register: it is a latch, not a
// live sample, and a transfer never touches it, so the status has to be ASKED for.  None of
// the three touches the ГРП free bit -- they fall through disk_ctl()'s switch, unlike the
// selects -- which is what makes them safe from inside the error path of a live request.
#define MDCMD_STATCLR 010 // clear the status register
#define MDCMD_STATLO  011 // latch bits 1-12
#define MDCMD_STATHI  031 // latch bits 13-24 -- RETURNED SHIFTED DOWN BY 12

// The status register, at the TRUE bit positions of the 24-bit register.  MDCMD_STATHI's
// half arrives pre-shifted down by twelve and mdstat() puts it back: unshifted,
// MDST_POWERUP >> 12 and MDST_READY are both 0400 and a test for one would answer the other.
//
// WHICH OF THESE SIMH ACTUALLY COMPUTES is not what the documentation says.  Only two are
// usable -- MDST_READY (it tests UNIT_ATT) and MDST_READONLY (UNIT_RO).  MDST_ABSENT is
// ALWAYS SET, attached or not: besm6_disk.c tests UNIT_DISABLE (02000), SIMH's "this unit
// CAN be disabled" capability bit, not UNIT_DIS (04000).  MDST_POWERUP is NEVER set, sitting
// on the `else if' MDST_ABSENT just took.  So a driver using MDST_ABSENT as its presence
// test would fail every request on a good disk -- bite-tested, mdtest run 1 fails on every
// drive.  This driver consults neither: the EXT_IOERR mask is the presence test.
//
// The rest are declared by the simulator and set by nothing, as on the drum.  They are named
// because the retry path is written against them for the real machine's sake.
#define MDST_SEEK       00000377  // 8-1: "seek done", one bit per unit
#define MDST_READY      00000400  // 9: the selected unit is ready         -- SIMH sets this
#define MDST_SEEK_FAIL  00001000  // 10: head location unknown
#define MDST_CHECKSUM   00002000  // 11: bad checksum on read
#define MDST_FAILURE    00004000  // 12: failure -- OR of some of the upper bits
#define MDST_MAYDAY     00010000  // 13: unspecified failure
#define MDST_NO_AMRK    00020000  // 14: address marker not found in a revolution
#define MDST_WRONG_CYL  00040000  // 15: wrong address marker
#define MDST_WRONG_ID   00100000  // 16: bad track ID
#define MDST_BAD_ACSUM  00200000  // 17: bad checksum of the address marker
#define MDST_UNFINISHED 00400000  // 18: I/O not finished after a revolution
#define MDST_TRK_PARITY 01000000  // 19: track parity error in two-track I/O
#define MDST_READONLY   02000000  // 20: the unit is read-only           -- SIMH sets this
#define MDST_POWERUP    04000000  // 21: powered up -- DEAD, see above
#define MDST_ABSENT     010000000 // 22: not connected -- ALWAYS SET, see above
#define MDST_BUF_ERR    020000000 // 23: transfer buffer not ready

// How many times one exchange is re-issued before the request is failed.  The v7 figure,
// from rk.c and rp.c.
#define MDRETRY 10

struct buf rmdbuf;
struct buf mdtab;

// The state of the one exchange that can be outstanding -- the driver's rather than the
// buf's because mdtab is a one-request-at-a-time queue head, in the v7 way.
static unsigned mddone; // words of mdtab.b_actf's request already transferred
static unsigned mdnw;   // words the exchange now running will move
static unsigned mdarm;  // the ГРП bit currently armed in МГРП, or 0
static unsigned mderr;  // the running exchange already failed; its completion is still due

// The status register as of the last failure, reassembled from its two halves.  Not static:
// kernel/test/mdtest reads it to check that a write-protected drive was told apart from an
// absent one, the way it already reaches into intr.c for mgrp.
unsigned mdstatus;

// The pack label -- service word 1, the magic mark and volume number -- one per drive.  The
// service-word buffer belongs to the CONTROLLER, so without this a write to md01 puts back
// whatever the last read of md00 left there.  Filled by every read and by mdlabel(), which
// is why it needs no "not seen yet" case: every writer opens the drive first, and an open
// that could not read the label is a drive that will not take the write either.  Indexed by
// minor(b_dev), which mdstrategy() has already bounded below MDNUNIT.
static int mdvol[MDNUNIT];

// Exchanges re-issued since the driver last started a fresh request.  The one thing that
// makes the hard/soft split above OBSERVABLE: both end in a failed request with the same
// b_resid, so a driver that retried nothing would look identical from outside.
// kernel/test/mdtest run 4 asserts on it.
unsigned mdretries;

static void mdstart(void);
void mdstrategy(struct buf *bp); // mdlabel() queues its own request through it

// Ask the controller for half of its status register.  `cmd' is MDCMD_STATLO or MDCMD_STATHI.
//
// ONLY MEANINGFUL WITH A UNIT SELECTED, and both traps bite silently: MDCMD_STATLO with no
// selection answers ~0, every error bit at once, and MDCMD_STATHI does not answer at all,
// leaving the previous query in the latch.  The caller is safe because mdstart() selects
// before every exchange; a query from anywhere else would need its own select first.
//
// `ctlr +' folds into the instruction's address field -- see mdstart().
static unsigned mdstat(unsigned ctlr, unsigned cmd)
{
    __besm6_ext(ctlr + EXT_DISKCTL3, cmd);
    return __besm6_ext(ctlr + EXT_DISKSTAT3, 0);
}

// Read block 0 of a drive nothing has read yet, to prime mdvol[] before the first write --
// task 37.  Here rather than in mdstrategy() because this SLEEPS: open is process context,
// while the strategy routine runs with interrupts blocked, from bawrite() and the pageout
// path.  One exchange per drive per boot.  Two opens racing both issue it; harmless.
static void mdlabel(dev_t dev)
{
    register struct buf *bp;
    int oerr;

    if (mdvol[minor(dev)] != 0)
        return;

    // geteblk() leaves the buffer on bfreelist's chain, not the device's, so no getblk()
    // can answer this from the cache: the read is always physical.
    bp           = geteblk();
    bp->b_dev    = dev;
    bp->b_blkno  = 0;
    bp->b_wcount = BSIZEW;
    bp->b_flags |= B_READ;
    mdstrategy(bp);

    // A label that would not read is not an open failure: mdvol[] stays 0 and the drive
    // behaves as it did before.  iowait()'s geterror() would otherwise leave it in u_error.
    oerr = u.u_error;
    iowait(bp);
    u.u_error = oerr;

    bp->b_dev = (dev_t)NODEV;
    brelse(bp);
}

void mdopen(dev_t dev, int rw)
{
    if ((unsigned)minor(dev) >= MDNUNIT) {
        u.u_error = ENXIO;
        return;
    }

    // Non-zero rw is an open that may write, at all three callers: openi() passes
    // mode & FWRITE, iinit() 1, smount() !ronly.  A read open primes mdvol[] by itself.
    if (rw)
        mdlabel(dev);
}

void mdstrategy(register struct buf *bp)
{
    register struct buf *dp;
    int s;

    // Both ends of a transfer have to be half-zone-grained: neither the track-address
    // command nor CW_PAGE|DISK_HALFPAGE has room for an offset within one.  The buffer cache
    // always obliges -- buffers[] is page-aligned and every buffer is BSIZEW long -- but a
    // raw physio() through /dev/md need not, and must be refused rather than land 511 words
    // from where it asked.
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
        mdretries  = 0; // the count belongs to one request; this one starts a fresh queue
        dp->b_actf = bp;
    } else
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

// Issue the next exchange of the request at the head of the queue, and keep issuing until
// one of them actually starts.  Called with the queue non-empty and interrupts blocked --
// from mdstrategy() at spl6, and from mdintr() with delivery held off by the gate.
//
// The loop is here rather than a tail call because a queue full of requests for a drive
// nobody attached would otherwise recurse once per request.
static void mdstart(void)
{
    register struct buf *bp;
    unsigned pa, blk, dev, ctlr, group, unit, zone, track, cw, cmd, bit;
    int *sys;

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
            // Zone-aligned, page-aligned and a whole zone left: one exchange.
            cw |= CW_PAGE_MODE;
            mdnw = PGSZ;
        } else {
            // One half-zone.  DISK_HALFPAGE picks the half of the MEMORY page; the half of
            // the ZONE rides in bit 1 of the track-address command below, and NOT in
            // DISK_HALFZONE, which the controller does not read (sys/besm6disk.h).
            if (pa & MDTRACK)
                cw |= DISK_HALFPAGE;
            mdnw = MDTRACK;
        }
        cmd = MDCMD_TRACK | (zone << 1) | track;

        // THE SECTOR HEADER IS WRITTEN FROM MEMORY, so a write has to supply it.  The
        // exchange moves the zone's service words along with the data in BOTH directions: a
        // read fills the four at 4*track from the platter, a write puts them back.  They are
        // the drive's own sector header, and the driver owns the copy between exchanges.
        //
        // TWO of each four are the driver's.  Word 0 is the sector's own address, bits 48-37,
        // the field the drive matches when looking for a track; it changes every exchange.
        // Word 1 is the volume's mark and number, constant within a pack but not the
        // CONTROLLER's to supply -- mdvol[] holds the drive's own.  Words 2 and 3, the userid
        // and the checksum, are left as the last read brought them in.
        //
        // Both halves of that were learned the hard way.  Until task 25b nothing wrote the
        // address, so every zone written got whatever zone was read last (kernel/test/session
        // found it, SIMH never reading it back).  The mark was left alone until block 0
        // became the superblock: task C4c measured the damage on two drives, cmd/mkfs is the
        // account.  And mdvol[] was filled only by a completed read until task 37, so a pack
        // only ever written got another drive's mark, and was left alone on the theory that
        // no path reached it -- `tar cf /dev/rmd1' did.  mdlabel() reads the label at open
        // now, so there is a drive's own mark to stamp unconditionally.
        if ((bp->b_flags & B_READ) == 0) {
            sys = MDSYS + ctlr * MDSYSWORDS;
            if (cw & CW_PAGE_MODE) {
                // A whole zone moves all eight words: both half-zones, both addresses.
                sys[0]             = (int)(zone * MDTPZ) << MDSYS_ADDR;
                sys[MDSYSHALF]     = (int)(zone * MDTPZ + 1) << MDSYS_ADDR;
                sys[1]             = mdvol[dev];
                sys[MDSYSHALF + 1] = mdvol[dev];
            } else {
                sys[track * MDSYSHALF]     = (int)blk << MDSYS_ADDR;
                sys[track * MDSYSHALF + 1] = mdvol[dev];
            }

            // AND DRAIN THE БРЗ: the controller reads MEMORY, and the CPU's write cache is
            // not memory.  The stores above are nowhere near eight instructions old, so
            // without this the platter gets the header the buffer held BEFORE this exchange
            // -- what kernel/test/session saw, and only under `set mmu cache'.
            //
            // It covers the data too, and that is why it sits here rather than beside the
            // stores: any write whose last touch of the buffer was fewer than eight stores
            // ago -- an inode update, a directory slot -- has the same problem, and would
            // show up as a lost update rather than a bad header.  Reads need none of it.
            drainbrz();
        }

        // `ctlr +' selects the controller: a VARIABLE PLUS A CONSTANT is the shape the
        // compiler folds into the instruction's own address field, leaving `wtc ctlr' beside
        // it.  The other way round it does not fold (dev/mb.c, doc/Intrinsics.md §8).
        //
        // The order is the header comment's: both selects (which RAISE the free bit) before
        // the control word (which lowers it), and the track address last.
        __besm6_ext(ctlr + EXT_DISKCTL3, MDCMD_GROUP | group);
        __besm6_ext(ctlr + EXT_DISKCTL3, MDCMD_UNIT | (1 << unit));
        __besm6_ext(ctlr + EXT_DISK3, cw);
        __besm6_ext(ctlr + EXT_DISKCTL3, cmd);

        // Did the drive take it?  An unattached unit transfers nothing and interrupts
        // never, so this poll is the only thing between a missing disk and a kernel waiting
        // forever.  It has to come after the track-address command: that command is what
        // sets the mask, and the control word before it is what cleared it.
        mderr = 0;
        if (__besm6_ext(EXT_IOERR, 0) & IOERR_DISK(ctlr)) {
            // It did not.  The answer decides something sharper than a retry policy:
            // WHETHER A COMPLETION IS STILL COMING.
            //
            //   No completion.  An unattached unit, and a WRITE to a read-only one, are
            //   refused outright -- disk_ctl() sets the mask and returns BEFORE arming the
            //   event.  Arming the ГРП bit here would be the hang this poll prevents, and
            //   retrying is not merely futile but wrong.
            //
            //   Completion still due.  A transfer attempted and failed partway leaves the
            //   mask set but the event armed, so the interrupt still arrives.  Re-issuing
            //   now would race it, so the retry belongs in mdintr(), once it has been
            //   consumed; mderr carries the fact across.
            //
            // MDST_READY and MDST_READONLY are the tests because they are the only two bits
            // this simulator computes correctly -- the table above.
            mdstatus = mdstat(ctlr, MDCMD_STATLO) & 07777;
            mdstatus |= (mdstat(ctlr, MDCMD_STATHI) & 07777) << 12;

            if ((mdstatus & MDST_READY) == 0 ||
                ((bp->b_flags & B_READ) == 0 && (mdstatus & MDST_READONLY) != 0)) {
                // Hard.  Give up on this request and try the next one.
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
            mgrpoff(mdarm); // the request crossed from one controller to the other
        mdarm = bit;
        mgrpon(bit);
        mdtab.b_active = 1;
        dk_busy |= 1 << DK_MD;
        return;
    }

    if (mdarm != 0) {
        mgrpoff(mdarm);
        mdarm = 0;
    }
    mdtab.b_active = 0;
    dk_busy &= ~(1 << DK_MD);
}

// An exchange finished: GRP_CHAN3_FREE or GRP_CHAN4_FREE, from the ГРП dispatch in
// kernel/intr.c.
void mdintr(void)
{
    register struct buf *bp;
    unsigned dev, blk;

    // A completion nobody is waiting for.  It cannot come from this driver, but the bit is
    // WIRED: returning would leave it standing and extintr() would call us forever.
    // Disarming is the only way to stop it.  kernel/test/mdtest forges exactly this.
    if (mdtab.b_active == 0) {
        mgrpoff(GRP_CHAN3_FREE | GRP_CHAN4_FREE);
        mdarm = 0;
        return;
    }

    bp = mdtab.b_actf;

    // This exchange had already reported an error, and waiting for its completion first is
    // the whole point of mderr: the transfer was attempted, so the interrupt was always
    // coming, and re-issuing before consuming it would race two completions for one armed
    // bit.  Now it is safe.  mddone is untouched, so mdstart() re-issues exactly the chunk
    // that failed, and must NOT disarm first -- its control word lowers the wired bit itself.
    //
    // NONE OF THIS RUNS UNDER SIMH in the ordinary way: the simulator models no checksum,
    // marker or parity failure, and the only route to a soft error is a read past the end of
    // the backing file (mdtest's short-image run).  This arm is written for the real machine.
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

    mdtab.b_errcnt = 0; // this chunk landed; the next one starts its own count

    // A read that landed is the one moment the service words are THIS drive's own header:
    // keep the mark for mdstart() to put back.  mdstart()'s address arithmetic, repeated --
    // mddone still describes the chunk that landed, and a half-zone read fills only the four
    // words at 4*track.  No drainbrz(): the device was the writer and this only reads.
    if (bp->b_flags & B_READ) {
        dev        = minor(bp->b_dev);
        blk        = (unsigned)bp->b_blkno + mddone / MDTRACK;
        mdvol[dev] = MDSYS[(dev >> 5) * MDSYSWORDS + (blk % MDTPZ) * MDSYSHALF + 1];
    }

    // Counted here rather than at the 033: mdstart() re-issues a soft failure, so a count at
    // issue would charge a retried chunk twice and credit words that never arrived, and a
    // request the start loop refused outright began no exchange and is counted nowhere.
    // mdnw is what the controller was ASKED to move -- the hardware gives no residual count,
    // so dk_wds means "words moved by exchanges that completed".
    dk_numb[DK_MD]++;
    dk_wds[DK_MD] += mdnw;

    mddone += mdnw;
    if (mddone < bp->b_wcount) {
        // More to move.  Do NOT disarm: mdstart()'s control word lowers the bit itself, the
        // arm/lower pairing the wired bits require.  Its selects raise the bit first, but
        // delivery is still blocked and the control word puts it back down -- which is the
        // whole reason the selects come first.
        mdstart();
        return;
    }

    mddone       = 0;
    bp->b_resid  = 0;
    mdtab.b_actf = bp->av_forw;
    iodone(bp);
    mdstart(); // which disarms if the queue has run dry
}

void mdread(dev_t dev)
{
    physio(mdstrategy, &rmdbuf, dev, B_READ);
}

void mdwrite(dev_t dev)
{
    physio(mdstrategy, &rmdbuf, dev, B_WRITE);
}
