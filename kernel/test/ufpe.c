// ufpe -- the two ARITHMETIC faults, from USER mode, on the real machine.
//
// utrap covers the same door (0500) for the protection faults; this one covers the causes it
// cannot raise, and it is the third test that LINKS THE CODE UNDER TEST rather than a copy of
// it: kernel/trap.c itself decodes here, and everything it calls is stubbed below the way
// usig.c stubs sendsig()'s neighbours.
//
// Three faults arrive, and the assertion is what trap() makes of each:
//
//   1. a floating overflow  (1e18 * 1e18)  -> SIGFPE, from the RIGHT half of its word
//   2. a floating divide by zero           -> SIGFPE, from the LEFT half
//   3. a read of a closed page             -> SIGSEGV, and the "done" signal
//
// Between them, uprog stores a mark that only executes if the resume landed PAST the faulting
// instruction.  That is the property these two causes have and the other five do not: the
// machine advances the PC and leaves SPSW_NEXT_RK clear, so trap()'s restart fix-up is a no-op
// and a returning handler makes progress instead of re-faulting forever.  Reaching panic()
// means the decode fell through -- which is what the whole arm exists to stop.
//
// ufpe.ini asserts ACC == 0.  A nonzero ACC names the failing check -- see the F_* bits.

#include <besm6.h>

#include "sys/besm6dev.h"
#include "sys/dir.h"
#include "sys/param.h"
#include "sys/proc.h"
#include "sys/reg.h"
#include "sys/signal.h"
#include "sys/systm.h"
#include "sys/text.h"
#include "sys/types.h"
#include "sys/user.h"

// `u' is reserved in crt0f.S, not declared here: the frame and trap()'s C frames grow up out of
// it (see the .bss comment there).
int maxmem = 512 * 1024;

static struct proc pr;
static struct text tx;

// crt0f.S
extern unsigned uprogadr; // uprog's link-time word address, as a plain integer
extern int *ustkbase;     // the gate's stack base: set to u.u_stack by main()
void gouser(unsigned uentry);
void halt(unsigned mask);

// brz.s
void drainbrz(void);

// Must match the EQUs in crt0f.S.
#define MARK1 0525252U
#define MARK2 0313131U

#define IMAGEPG 16 // physical page of the process image (data + stack), free memory

// Physical base of the data region == the physical address of virtual page 2 (CBASE).
#define DBASE (IMAGEPG * PGSZ + USIZE)

// Fault-mask bits, reported in the accumulator by halt().  Zero means every check passed.
#define F_SIG1   0001  // fault 1 did not become SIGFPE
#define F_SIG2   0002  // fault 2 did not become SIGFPE
#define F_SIG3   0004  // fault 3 did not become SIGSEGV
#define F_MARK1  0010  // fault 1 resumed ON the multiply, not past it
#define F_MARK2  0020  // fault 2 resumed ON the divide, not past it
#define F_NSIG   0040  // more faults than the three arranged: a resume that re-faults
#define F_PANIC  0100  // trap() reached its panic: the cause was not decoded
#define F_GROW   0200  // grow() was asked to extend something other than fault 3's page

static unsigned mask; // accumulated failures
static unsigned nsig; // which fault we are in: 1, 2, 3

// ---- the neighbours kernel/trap.c calls -----------------------------------------------------

char runrun;
char curpri;

// The observable.  trap() has already decoded and dismissed the cause by the time it gets here,
// so the SIGNAL is what this test reads -- which is the thing under test in any case.
void psignal(struct proc *p, int sig)
{
    (void)p;
    nsig++;
    switch (nsig) {
    case 1:
        if (sig != SIGFPE)
            mask |= F_SIG1;
        return;
    case 2:
        if (sig != SIGFPE)
            mask |= F_SIG2;
        return;
    case 3:
        if (sig != SIGSEGV)
            mask |= F_SIG3;
        break;
    default:
        mask |= F_NSIG;
        break;
    }

    // Done.  uprog's marks were MAPPED stores and these are physical reads, so settle the
    // write cache first.
    drainbrz();
    if (*(volatile unsigned *)(DBASE + 2) != MARK1)
        mask |= F_MARK1;
    if (*(volatile unsigned *)(DBASE + 3) != MARK2)
        mask |= F_MARK2;
    halt(mask); // never returns
}

// Fault 3 only: refuse, so trap() falls through to SIGSEGV.
int grow(int pg)
{
    if (pg != 6)
        mask |= F_GROW;
    return 0;
}

void panic(char *s)
{
    (void)s;
    halt(mask | F_PANIC); // never returns
}

int issig(void)
{
    return 0;
}

void psig(void)
{
}

int setpri(struct proc *pp)
{
    (void)pp;
    return 0;
}

void qswtch(void)
{
}

// trap() announces every signal and dumpregs() prints; neither is under test here, and this
// test links no console driver.
void printf(char *fmt, ...)
{
    (void)fmt;
}

// ---- the environment ------------------------------------------------------------------------

int main()
{
    unsigned uaddr, uentry;

    // The gate switches to u.u_stack, which is where the real trap() looks for its frame.
    ustkbase = u.u_stack;

    // The same map utrap builds: uprog's own physical page at virtual page 0, text two pages,
    // data two -- so virtual page 6 is closed and fault 3 lands there.
    uaddr  = uprogadr;
    uentry = uaddr & (PGSZ - 1);

    tx.x_caddr = uaddr & ~(PGSZ - 1);
    tx.x_size  = 2 * PGSZ;

    pr.p_addr  = IMAGEPG * PGSZ;
    pr.p_size  = USIZE + 4 * PGSZ + PGSZ;
    pr.p_textp = &tx;

    u.u_procp = &pr;
    u.u_tsize = 2 * PGSZ;
    u.u_dsize = 2 * PGSZ;
    u.u_ssize = PGSZ;

    sureg();

    // The constants uprog computes with, seeded physically (the kernel runs unmapped) into what
    // the user sees at CBASE.  1e18 is a little under DBL_MAX, so its square overflows by a wide
    // margin; the zero beside it is the divisor.
    *(volatile double *)DBASE         = 1e18;
    *(volatile double *)(DBASE + 1)   = 0.;
    *(volatile unsigned *)(DBASE + 2) = 0;
    *(volatile unsigned *)(DBASE + 3) = 0;
    *(volatile unsigned *)(DBASE + 4) = MARK1;
    *(volatile unsigned *)(DBASE + 5) = MARK2;
    drainbrz();

    // Mask every interrupt source: the forged SPSW enters user mode with БлПр clear and the
    // interval timer re-arms GRP_TIMER at reset.
    __besm6_mod(MOD_MGRP, 0);

    gouser(uentry); // never returns
    return 0;
}
