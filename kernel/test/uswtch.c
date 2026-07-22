/*
 * uswtch -- save(), resume(), idle() and the u-area invariant, on the real machine.
 *
 * The fifth standalone SIMH test, and the third (after usys and uclock) to link the code
 * under test rather than a copy: the kernel's own switch.o (save/resume), uarea.o
 * (uflush/uload), brz.o (drainbrz) and intr.o (spl/idle/extintr), with only
 * the environment hand-built.  crt0w.S puts `u' at the real 076000 and runs main() on the
 * u-page stack, because resume() reloads that very page out from under its caller.
 *
 * Three legs:
 *
 *   A  save()/resume() through the FAST path (paddr == uhome, nothing copied): do the nine
 *      label slots come back?  Driven from crt0w.S's regtest(), since C cannot spend r1-r7.
 *
 *   B  the invariant itself.  Two homes above 0100000; a sentinel written into the live
 *      u-area by "process A" must be ABSENT after switching to B and PRESENT again when A is
 *      resumed.  This is the leg that needs `set mmu cache': uflush/uload copy through a
 *      stolen window, and their drains are invisible without it.
 *
 *   C  idle().  With the interval timer free-running, idle() must RETURN -- the spin is
 *      released by extintr() clearing `idling' -- and must leave the level as it found it.
 *      This is also the first time in this tree that an interrupt is taken in KERNEL mode:
 *      until БлПр became the ipl (task 16d-pre), the gates held it and nothing could be.
 *
 * The phase counter lives in ordinary bss, NOT in the u-area: leg B swaps the u-area under
 * itself, so a phase kept there would be swapped too and the alternation would never end.
 */

// clang-format off
#include "sys/types.h"
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/dir.h"
#include "sys/user.h"
#include "sys/proc.h"
#include "sys/besm6dev.h"
// clang-format on

#include <besm6.h>

/*
 * From crt0w.S: the register-file probe and the cells it reports through.  `u' comes from
 * there too, as the absolute symbol 076000 -- this file must not define it.
 */
int regtest(label_t lab);
void halt(int status);
void drainbrz(void);
struct trap;
extern int rt_r1, rt_r2, rt_r3, rt_r4, rt_r5, rt_r6, rt_r7, rt_r15;

/* intr.c hands this to clock(); no frame is built here, so it stays 0 and is unread. */
int *intrframe;

/*
 * The two process images, one page each (USIZE == PGSZ == 1024), above 0100000 and so out of
 * reach of an unmapped access -- which is the whole reason uflush/uload need a window.
 */
#define P0 (32 * PGSZ) /* 0100000 */
#define P1 (33 * PGSZ) /* 0102000 */

#define SENT_A 0525252 /* "the live u-area belongs to A" */
#define SENT_B 0252525

/* The sentinel rides in a struct user field this test has no other use for. */
#define sentinel u.u_arg[0]

/* Failure bits, mirrored in uswtch.ini's legend. */
#define F_SAVE0   00001 /* save() did not return 0 on the direct call */
#define F_REGS    00002 /* leg A: r1-r7 did not survive */
#define F_R15     00004 /* leg A: r15 did not survive */
#define F_PHASE   00010 /* leg B: the alternation did not run to completion */
#define F_SENTB   00020 /* leg B: B saw A's sentinel -- the u-area did not switch */
#define F_SENTA   00040 /* leg B: A did not get its own u-area back */
#define F_UHOME   00100 /* leg B: uhome did not end up where it should */
#define F_IDLE    00200 /* leg C: idle() did not return, or `idling' was left set */
#define F_IPL     00400 /* leg C: idle() did not restore the level */
#define F_NOTICK  01000 /* leg C: no interrupt was ever dispatched */

static int mask;
static int phase;  /* leg B's state -- in bss, deliberately: see the header */
static int nticks; /* bumped by the clock() stub below */

/*
 * The environment intr.o names.  clock.o is NOT linked: this test has no business running
 * the real clock, and a stub lets leg C prove the dispatch without dragging in callouts,
 * signals and proc[] aging (which is uclock's job, and uclock already does it).
 */
void clock(struct trap *tr)
{
    (void)tr;
    nticks++;
}

void scintr(void)
{
    /* no console driver here; ПРП is never enabled, so this must never run */
}

void mbintr(void)
{
    /* no drum driver here either; extintr() names it, nothing arms its bits */
}

void mdintr(void)
{
    /* nor a disk driver; likewise named by extintr() and likewise never reached */
}

int main()
{
    int s;

    /*
     * Leg A.  uhome must already name the live u-area, so that regtest()'s resume() takes
     * the fast path: what this leg is about is the register file, not the copy.
     */
    uhome = P0;
    if (regtest(u.u_rsav) != 0)
        mask |= F_SAVE0;
    if (rt_r1 != 01111 || rt_r2 != 02222 || rt_r3 != 03333 || rt_r4 != 04444 ||
        rt_r5 != 05555 || rt_r6 != 06666 || rt_r7 != 07777)
        mask |= F_REGS;
    if (rt_r15 == 0)
        mask |= F_R15;

    /*
     * Leg B.  Build P1's home out of the live u-area, so that it holds a label that is valid
     * to resume through -- this is newproc() in miniature, and the flush has to happen just
     * after the save() for exactly newproc()'s reason.  The sentinel is set BEFORE the
     * flush, so P1's copy carries B's, and changed to A's afterwards, so the live one
     * carries A's.  Then the two must not be confusable.
     */
    sentinel = SENT_B;
    if (save(u.u_rsav) == 0) {
        uflush(P1);        /* P1's home := this u-area, label and stack and all */
        sentinel = SENT_A; /* ... the live one is A's from here on */
        drainbrz();
        phase = 1;
        resume(P1, u.u_rsav); /* flush live -> P0, load P1 -> live; no return */
    }

    if (phase == 1) {
        /* We are B: the u-area came from P1's home, so its sentinel must be B's. */
        if (sentinel != SENT_B)
            mask |= F_SENTB;
        if (uhome != P1)
            mask |= F_UHOME;
        phase = 2;
        resume(P0, u.u_rsav); /* ... and back again */
    }

    if (phase == 2) {
        /* And back in A, with A's u-area returned from P0's home. */
        if (sentinel != SENT_A)
            mask |= F_SENTA;
        if (uhome != P0)
            mask |= F_UHOME;
    } else {
        mask |= F_PHASE;
    }

    /*
     * Leg C.  Arm the sources, drop to spl6 the way swtch()'s loop does, and idle().  The
     * interval timer free-runs at HZ and cannot be switched off, so a tick is guaranteed;
     * idle() opens БлПр itself through spl0(), extintr() clears `idling', and the spin ends.
     * If any of that is wrong this hangs rather than failing -- which the .ini's step limit
     * turns into a failure.
     */
    intrinit();
    s = spl6();
    idle();
    if (idling)
        mask |= F_IDLE;
    if (nticks == 0)
        mask |= F_NOTICK;
    /* idle() must put back the level it was called at: spl6 blocks, so spl0() returns 6. */
    if (spl0() != 6)
        mask |= F_IPL;
    splx(s);

    halt(mask);
    return 0;
}
