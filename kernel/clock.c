// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

#include "sys/callo.h"
#include "sys/dir.h"
#include "sys/param.h"
#include "sys/proc.h"
#include "sys/reg.h"
#include "sys/seg.h"
#include "sys/systm.h"
#include "sys/types.h"
#include "sys/user.h"

#define SCHMAG 8 / 10

// Ticks between two accruals of p_cpu.  HZ/60 rounded: this machine's tick is four
// times v7's, and p_cpu is the one thing in the kernel that cares.  See the block
// comment at the accrual below -- it must be a power of two, and the mask there
// assumes it.
#define CPUTICK 4

int lbolt;                   // ticks since the last second rolled over: 0..HZ-1, not in `time'
struct callo callout[NCALL]; // the callout table; sys/callo.h only declares it

// clock is called straight from
// the real time clock interrupt.
//
// Functions:
// 	implement callouts
// 	maintain user/system times
// 	maintain date
// 	profile
// 	lightning bolt wakeup (every second)
// 	alarm clock signals
// 	jab the scheduler
//
// The frame comes by pointer, from the `intrframe' cell the 0501 gate publishes it in.
// clock() cannot find it the way trap() does: the interrupt gate's stack switch is
// conditional, so the frame sits at the base of the kernel stack only when the tick came
// from user -- when it nested inside a syscall, u.u_stack holds the syscall's frame, whose
// SPSW says user, and the USERMODE test below would charge the wrong bucket.  Nor does it
// come through u.u_ar0: that names the USER's registers and belongs to whatever the tick
// interrupted.  See the header over intrgate in besm6.S.
//
// One thing this reads is still inert: addupc() is a stub (besm6.S) and u_prof.pr_scale is 0
// until profil() works -- task 17.  Idle-time accounting is live, though: `idling' is raised
// by the idle spin in intr.c and cleared by extintr() once this has run.
void clock(struct trap *tr)
{
    register struct callo *p1, *p2;
    register struct proc *pp;
    int a;

    // callouts
    // if none, just continue
    // else update first non-zero time

    if (callout[0].c_func == NULL)
        goto out;
    p2 = &callout[0];
    while (p2->c_time <= 0 && p2->c_func != NULL)
        p2++;
    p2->c_time--;

    // if saved pl is high, just return
    if (BASEPRI(0))
        goto out;

    // callout

    spl5();
    if (callout[0].c_time <= 0) {
        p1 = &callout[0];
        while (p1->c_func != 0 && p1->c_time <= 0) {
            (*p1->c_func)(p1->c_arg);
            p1++;
        }
        p2 = &callout[0];
        while ((p2->c_func = p1->c_func)) {
            p2->c_time = p1->c_time;
            p2->c_arg  = p1->c_arg;
            p1++;
            p2++;
        }
    }

    // lightning bolt time-out
    // and time of day
out:
    a = dk_busy & 07;
    if (USERMODE(tr->spsw)) {
        u.u_utime++;
        if (u.u_prof.pr_scale)
            addupc(tr->ret, &u.u_prof, 1);
        if (u.u_procp->p_nice > NZERO)
            a += 8;
    } else {
        a += 16;
        if (idling) // the idle spin, intr.c
            a += 8;
        u.u_stime++;
    }
    dk_time[a] += 1;
    pp = u.u_procp;

    // ACCRUE p_cpu ONE TICK IN FOUR, NOT EVERY TICK.  The pair of rates below is
    // v7's and only one half of it is a tick count: p_cpu goes UP once per tick here
    // and is decayed by SCHMAG (8/10) once per SECOND in the arm just below.  So the
    // value a process settles at is the fixed point of x = (x + f*rate)*8/10, i.e.
    // 4*f*rate, where f is its share of the CPU and `rate' is how often this line
    // runs.  v7 ran it at 60 Hz: a CPU hog settled at 240, just under the 255 the
    // char saturates at, and setpri()'s p_cpu/16 (slp.c) spread 0%..100% of the CPU
    // across sixteen priorities.  That band IS the scheduler's resolution.
    //
    // At HZ = 250 the fixed point is 1000, and 255 is reached at f = 0.255: every
    // process above a quarter of the CPU pins p_cpu and setpri() cannot tell any two
    // of them apart.  The decay is per second and cannot be rescaled -- it is what
    // makes p_time, alarm() and the lightning bolt seconds -- so the accrual is the
    // half that has to move.  One tick in CPUTICK puts the rate at 63/s and the
    // fixed point at 252, back inside v7's band.
    //
    // `lbolt' is still this second's count BEFORE the ++ below, so it runs 0..HZ-1
    // and the mask fires on 0, 4, ... 248: 63 times a second.  A mask, not a %: a
    // modulo by anything but a power of two is a b$div call on this machine.
    //
    // Switching the kernel to the machine's 62.5 Hz slow clock (ГРП bit 10) would
    // buy exactly this and nothing else, at the price of an HZ that is no longer
    // exact -- see kernel/README.md, "Gotchas worth not re-deriving".
    if ((lbolt & (CPUTICK - 1)) == 0)
        if (++pp->p_cpu == 0)
            pp->p_cpu--;
    if (++lbolt >= HZ) {
        if (BASEPRI(0))
            return;
        lbolt -= HZ;
        ++time;
        spl1();
        runrun++;
        wakeup((chan_t)&lbolt);
        for (pp = &proc[0]; pp < &proc[NPROC]; pp++)
            if (pp->p_stat && pp->p_stat < SZOMB) {
                if (pp->p_time != 127)
                    pp->p_time++;
                if (pp->p_clktim)
                    if (--pp->p_clktim == 0)
                        psignal(pp, SIGCLK);
                a = (pp->p_cpu & 0377) * SCHMAG + pp->p_nice - NZERO;
                if (a < 0)
                    a = 0;
                if (a > 255)
                    a = 255;
                pp->p_cpu = a;
                if (pp->p_pri >= PUSER)
                    setpri(pp);
            }
        if (runin != 0) {
            runin = 0;
            wakeup((chan_t)&runin);
        }
    }
}

// timeout is called to arrange that
// fun(arg) is called in tim/HZ seconds.
// An entry is sorted into the callout
// structure. The time in each structure
// entry is the number of HZ's more
// than the previous entry.
// In this way, decrementing the
// first entry has the effect of
// updating all entries.
//
// The panic is there because there is nothing
// intelligent to be done if an entry won't fit.
void timeout(void (*fun)(carg_t), carg_t arg, int tim)
{
    register struct callo *p1, *p2;
    register int t;
    int s;

    t  = tim;
    p1 = &callout[0];
    s  = spl7();
    while (p1->c_func != 0 && p1->c_time <= t) {
        t -= p1->c_time;
        p1++;
    }
    if (p1 >= &callout[NCALL - 1])
        panic("Timeout table overflow");
    p1->c_time -= t;
    p2 = p1;
    while (p2->c_func != 0)
        p2++;
    while (p2 >= p1) {
        (p2 + 1)->c_time = p2->c_time;
        (p2 + 1)->c_func = p2->c_func;
        (p2 + 1)->c_arg  = p2->c_arg;
        p2--;
    }
    p1->c_time = t;
    p1->c_func = fun;
    p1->c_arg  = arg;
    splx(s);
}
