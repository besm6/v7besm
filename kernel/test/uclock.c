/*
 * uclock -- take a real timer interrupt in USER mode and check that it reached clock().  Task 15e.
 *
 * uintr proved the 0501 gate; this proves what is behind it.  It links the kernel's actual
 * kernel/intr.c and kernel/clock.c -- the way usys links the real syscall.c -- and hand-builds
 * only the environment they name: proc[], the u-area's neighbours, and recording stubs for the
 * signal and scheduling calls clock()'s once-a-second tail makes.
 *
 * The tick is raised with `увв 031' (GRP |= ACC << 24) while БлПр is still set, so it sits pending
 * until gouser()'s `выпр' and then fires at uprog's first instruction.  That makes the interesting
 * tick deterministic -- it does not depend on how fast the simulator runs.
 *
 * What one tick has to produce, and why each check is the right one:
 *   * THE CALLOUT FIRES.  timeout(tick_fired, MAGIC, 1) is armed before entry, so the tick's
 *     `p2->c_time--' takes it to zero and clock() calls it.  This is what the test exists for.
 *   * u_utime, NOT u_stime.  The interrupt came from user mode, so USERMODE(tr->spsw) must be true
 *     -- which is only so if clock() read the RIGHT frame, the one the gate published.
 *     `u_stime == 0' is the sharpest single check in the file: it fails if the frame pointer is
 *     stale, garbage, or the syscall-style guess at u.u_stack.
 *   * THE SECOND ROLLS OVER.  lbolt is seeded at HZ-1, so this tick takes the `++lbolt >= HZ' arm:
 *     ++time, runrun, wakeup(&lbolt), and the proc[] sweep that ages p_time and fires SIGCLK.
 *   * МГРП SURVIVES.  clock() leaves spl1 behind and nothing in the return-from-interrupt path
 *     restores the level (`выпр' restores БлПр, not МГРП), so extintr() has to.  If it does not,
 *     mgrp comes back 0 and the machine is deaf from the first tick on.
 *   * THE FRAME WENT TO ITS OWN CELL.  `intrframe' must hold the stack base (the tick came from
 *     user, so the gate's conditional switch had to fire) and u.u_ar0 must be untouched -- it
 *     belongs to whatever the tick interrupted, and in the real kernel that may be an exec() or a
 *     sendsig() about to write the resumed PC through it.
 *   * THE STACK WAS SWITCHED.  Physical USPV is seeded with KSENT.  extintr() + clock() + the
 *     proc[] sweep is a lot of C to run on the user's r15 by mistake.
 *
 * ONE EXTRA TICK IS POSSIBLE and every check above tolerates it.  The BESM-6 interval timer
 * free-runs at 250 Hz and re-arms itself; the SIMH CLK device carries no DEV_DISABLE flag, so no
 * `.ini' can stop it.  A second tick would find the callout already compacted out of callout[],
 * lbolt back at 0 (so no second rollover, hence no second ++time, p_time++ or SIGCLK), and
 * p_clktim already 0.  Only u_utime can grow, which is why that one check is `>=' and not `=='.
 *
 * uclock.ini asserts ACC == 0.  A nonzero ACC names the failing check -- see the F_* bits.
 */

// clang-format off
#include "sys/types.h"
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/callo.h"
#include "sys/dir.h"
#include "sys/user.h"
#include "sys/proc.h"
#include "sys/text.h"
#include "sys/reg.h"
#include "sys/seg.h"
#include "sys/besm6dev.h"
// clang-format on

#include <besm6.h>

/*
 * `u' is NOT defined here, unlike in mmutest/uintr: crt0c.S reserves it, because extintr() and
 * clock() run on the stack that grows out of u.u_stack and it needs room.  maxmem is what utab.o
 * wants; proc[] is what clock()'s once-a-second sweep walks.
 */
int maxmem = 512 * 1024;
struct proc proc[NPROC];

static struct text tx;

/* crt0c.S */
extern unsigned uprogadr; /* uprog's link-time word address, as a plain integer */
extern int *ustkbase;     /* base of the gate's stack; main() points it at u.u_stack */
void gouser(unsigned uentry);
void halt(unsigned mask);

/* brz.s */
void drainbrz(void);

/* intr.c: the shadow of МГРП, which cannot be read back.  The ipl check below reads it. */
extern unsigned mgrp;

/* crt0c.S: where intrgate published the frame base.  Still there after the tick. */
extern int *intrframe;

/* Must match the EQUs in crt0c.S. */
#define USPV  070010U /* forged r15, and the physical stack-switch sentinel */
#define KSENT 0333333U

#define IMAGEPG 16 /* physical page of the process image (data + stack), free memory */

/*
 * увв 031 simulates a ГРП interrupt: GRP |= (ACC & 0xFFFFFF) << 24.  Same deterministic source
 * uintr uses -- no device, no timing.  GRP_TIMER (bit 40) is a plain flip-flop (not one of
 * GRP_WIRED_BITS), so the real extintr() dismisses it with an ordinary MOD_GRPCLR.
 */
#define EXT_GRPSET 031U

/* The callout's argument: a value nothing else in the image could plausibly write. */
#define MAGIC ((carg_t)0246813U)

/* Fault-mask bits, reported in the accumulator by halt().  Zero means every check passed. */
#define F_STACK  0000001 /* the stack was not switched: the frame landed on the user r15 */
#define F_NOCALL 0000002 /* the callout never fired -- the tick did not reach clock() */
#define F_CALLN  0000004 /* it fired more than once, or with the wrong argument */
#define F_UTIME  0000010 /* user time was not charged */
#define F_STIME  0000020 /* system time WAS charged: clock() read the wrong frame */
#define F_TIME   0000040 /* the second did not roll over (++time) */
#define F_LBOLT  0000100 /* lbolt did not wrap back below HZ */
#define F_RUNRUN 0000200 /* the scheduler was not jabbed */
#define F_WAKEUP 0000400 /* nobody was woken on &lbolt */
#define F_PTIME  0001000 /* proc[0].p_time was not aged */
#define F_SIGCLK 0002000 /* the alarm did not fire exactly once, as SIGCLK */
#define F_SETPRI 0004000 /* setpri() was not called for the aged process */
#define F_MGRP   0010000 /* extintr() did not restore the ipl: МГРП came back masked */
#define F_FRAME  0020000 /* the gate published the wrong frame, or disturbed u.u_ar0 */

static unsigned mask;

/*
 * What the stubs and the callout recorded.
 */
static int ncallout;       /* how many times the callout ran */
static carg_t calloutarg;  /* the argument it ran with */
static int nsig, lastsig;  /* psignal() -- clock() raises SIGCLK when p_clktim expires */
static int nwakeup;        /* wakeup() calls */
static chan_t lastchan;    /* the last channel woken -- must be &lbolt */
static int nsetpri;        /* setpri() calls */
static unsigned mgrp_seen; /* mgrp as extintr() left it, sampled by the callout's successor */

/* ------------------------------------------------------------------------- */
/* The environment kernel/clock.c and kernel/intr.c name.                     */
/* ------------------------------------------------------------------------- */

char runrun;
char runin;
time_t time;
int dk_busy;
int dk_time[32];

/* intr.c calls this from prpintr(); no ПРП bit is ever up here, so it must never run. */
void scintr(void)
{
    mask |= F_NOCALL; /* reaching this at all means the dispatch went wrong */
}

/* Likewise the drum: extintr() names mbintr(), but no drum bit is ever armed here. */
void mbintr(void)
{
    mask |= F_NOCALL;
}

/* And the disk, for the same reason. */
void mdintr(void)
{
    mask |= F_NOCALL;
}

void wakeup(chan_t chan)
{
    nwakeup++;
    lastchan = chan;
}

void psignal(struct proc *p, int sig)
{
    nsig++;
    lastsig = sig;
}

int setpri(struct proc *pp)
{
    nsetpri++;
    return (0);
}

void addupc(int pc, void *prof, int incr)
{
}

void panic(char *s)
{
    halt(0777777); /* a distinctive, unmistakable failure */
}

/* The callout under test. */
static void tick_fired(carg_t arg)
{
    ncallout++;
    calloutarg = arg;
}

/* ------------------------------------------------------------------------- */

int main()
{
    unsigned uaddr, uentry;

    /*
     * The gate's stack IS the u-area's tail, as in the real kernel: extintr() and clock() run on
     * it.  crt0c.S leaves the cell for us to fill because the offset is not known to the assembler.
     */
    ustkbase = u.u_stack;

    /*
     * Build the user map: uprog's OWN physical page at virtual page 0 (so the forged user runs the
     * real code), text two pages, data two, one stack page at USTKPAGE (virtual 28) under the
     * forged r15.  Virtual page 4 is left closed: that is uprog's exit.
     */
    uaddr  = uprogadr;           /* uprog's WORD address (a plain integer from the linker) */
    uentry = uaddr & (PGSZ - 1); /* its offset within virtual page 0 */

    tx.x_caddr = uaddr & ~(PGSZ - 1); /* map virtual page 0 -> uprog's physical page */
    tx.x_size  = 2 * PGSZ;

    proc[0].p_addr  = IMAGEPG * PGSZ;
    proc[0].p_size  = USIZE + 2 * PGSZ + PGSZ;
    proc[0].p_textp = &tx;

    /*
     * proc[0] is the process, and it is in the REAL proc[] array rather than a private struct:
     * clock()'s once-a-second arm sweeps proc[0..NPROC-1] and only acts on entries with a live
     * p_stat, so a process off to the side would never be aged.  p_clktim = 1 arms the alarm this
     * tick expires; p_pri >= PUSER is what sends it through setpri().
     */
    proc[0].p_stat   = SRUN;
    proc[0].p_clktim = 1;
    proc[0].p_pri    = PUSER;

    u.u_procp = &proc[0];
    u.u_tsize = 2 * PGSZ;
    u.u_dsize = 2 * PGSZ;
    u.u_ssize = PGSZ;

    sureg();

    /*
     * The stack-switch sentinel: seed physical USPV/USPV+1.  If the gate failed to switch r15,
     * extintr()'s C frame would run on the user's r15 -- a physical index, БлП being forced on --
     * and overwrite this.
     */
    *(volatile unsigned *)USPV       = KSENT;
    *(volatile unsigned *)(USPV + 1) = KSENT;
    drainbrz();

    /*
     * Arm the one-tick callout.  timeout() with tim = 1 lands c_time = 1 in callout[0], so the
     * first tick's decrement takes it to zero and clock() runs it on the spot.
     */
    timeout(tick_fired, MAGIC, 1);

    /*
     * Seed lbolt one short of a full second, so this single tick also takes clock()'s once-a-second
     * arm -- ++time, runrun, wakeup, and the proc[] sweep.
     */
    lbolt = HZ - 1;

    /*
     * Dismiss whatever the free-running interval timer accumulated while we were setting up, arm
     * the sources, open the interrupt level through the REAL spl0() (so curipl is the one
     * extintr() will read), then raise the tick by hand.
     *
     * intrinit() is not optional: МГРП is armed once, there, and spl0() only clears БлПр.  Leave
     * it out and extintr()'s `grp & mgrp' sees nothing, the tick is never dispatched, and every
     * check below fails at once.
     *
     * Nor is the cli().  The whole design of this test is that the tick stays PENDING until
     * gouser()'s `выпр' enters user mode, so that clock() sees a user frame; that used to be free,
     * because БлПр was set from crt0c.S's PSW = 02003 and nothing in the kernel ever cleared it.
     * Now spl0() clears it for real, so without the cli() the tick fires here, in kernel mode,
     * inside main().  cli() re-blocks delivery without disturbing curipl, which is what the
     * splx() in extintr() will restore to; gouser() forges SPSW = 0, so the `выпр' opens БлПр at
     * the same instant it enters user mode.
     */
    __besm6_mod(MOD_GRPCLR, ~GRP_TIMER);
    intrinit();
    spl0();
    cli();
    __besm6_ext(EXT_GRPSET, (unsigned)(GRP_TIMER >> 24));

    gouser(uentry); /* forge the user context and enter it; never returns */
    return 0;
}

/*
 * Reached from crt0c.S's `toreport' after uprog's closing data fault.  Everything checked here is
 * kernel-side storage written in supervisor mode with БлП on, so it needs no bracket -- but the
 * sentinel was seeded before sureg() programmed the map, so drain the write cache first.
 */
void report(void)
{
    drainbrz();

    mgrp_seen = mgrp;

    /* The gate switched the stack: otherwise the frame landed on the user's r15. */
    if (*(volatile unsigned *)USPV != KSENT || *(volatile unsigned *)(USPV + 1) != KSENT)
        mask |= F_STACK;

    /* The done-condition: a tick reached clock() and the callout fired. */
    if (ncallout == 0)
        mask |= F_NOCALL;
    else if (ncallout != 1 || calloutarg != MAGIC)
        mask |= F_CALLN;

    /*
     * The frame clock() read really was the interrupted USER context.  u_utime may be charged
     * twice if the free-running timer slipped a second tick in; u_stime may never be charged at
     * all, and that is the check that bites when the frame pointer is wrong.
     */
    if (u.u_utime < 1)
        mask |= F_UTIME;
    if (u.u_stime != 0)
        mask |= F_STIME;

    /* The once-a-second arm ran: lbolt wrapped, the date advanced, the scheduler was jabbed. */
    if (time != 1)
        mask |= F_TIME;
    if (lbolt >= HZ)
        mask |= F_LBOLT;
    if (runrun == 0)
        mask |= F_RUNRUN;
    if (nwakeup < 1 || lastchan != (chan_t)&lbolt)
        mask |= F_WAKEUP;

    /* ... and swept proc[], aging proc[0] and expiring its alarm exactly once. */
    if (proc[0].p_time != 1)
        mask |= F_PTIME;
    if (nsig != 1 || lastsig != SIGCLK)
        mask |= F_SIGCLK;
    if (nsetpri < 1)
        mask |= F_SETPRI;

    /*
     * extintr() put back the ipl clock() raised and left.  Two halves, since the ipl moved onto
     * БлПр and МГРП became a source enable armed once by intrinit():
     *
     *   - mgrp still reads IRQ_ON, i.e. nothing rewrote the source mask behind intrinit()'s back;
     *   - the LEVEL came back, which is now curipl.  spl7() returns the level it displaces, and
     *     main() left it at 0 before gouser(), so anything else means clock()'s spl5()/spl1()
     *     leaked past extintr()'s repair.  (`выпр' restores the hardware bit by itself now; the
     *     software shadow is what still needs extintr() to fix it.)
     */
    if (mgrp_seen != (GRP_SLAVE | GRP_TIMER))
        mask |= F_MGRP;
    if (spl7() != 0)
        mask |= F_MGRP;

    /*
     * The frame really was at the base of the kernel stack -- the tick came from user, so the
     * gate's conditional switch had to fire -- and it went to the private cell, NOT to u.u_ar0.
     * u_ar0 belongs to whatever the tick interrupted; a gate that wrote it here would be silently
     * corrupting an interrupted exec() or sendsig() in the real kernel.
     */
    if (intrframe != ustkbase)
        mask |= F_FRAME;
    if (u.u_ar0 != 0)
        mask |= F_FRAME;

    halt(mask);
}
