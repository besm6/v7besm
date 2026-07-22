// usys -- exercise the extracode door (kernel/besm6.S: sysgate / badext, and the real
// kernel/syscall.c behind them) from USER mode, on the real machine.  Task 15d.
//
// uintr covers 0501 and utrap covers 0500; this one covers 0577 and 0550.  Unlike those two,
// IT LINKS THE CODE UNDER TEST rather than a copy of it: kernel/syscall.c is a separate file
// exactly so this test can pull in the real dispatcher.  What is hand-built here is only the
// environment the dispatcher needs -- a sysent[] of recording stubs, and stubs for the signal
// and scheduling calls its tail makes.
//
// Four legs (staged by uprog in crt0s.S):
//   1. A syscall with NO arguments.  Checks the two-value return: r_val1 in the accumulator,
//      r_val2 in r12, errno 0 in r14 -- and that r13 (the caller's return address) and r15 are
//      untouched.
//   2. A syscall with THREE arguments, staged the real way (arguments 1..2 descending below
//      r15, the third in the accumulator).  Checks that syscall() INVERTS that into the
//      ascending u_arg[0..2] the callee expects, and that it popped the two pushed words.
//   3. A syscall number outside sysent[].  Checks it is range-checked, not masked: -1 in the
//      accumulator and EINVAL in r14, from nosys().
//   4. An unimplemented extracode (э50).  Checks badext signals SIGINS and the program resumes
//      with its machine intact.
//
// Every leg also checks the INTERRUPT LEVEL: each handler reads PSW back with __besm6_getpsw()
// -- the mode word is the one machine register that can be read back -- and the report fails if
// БлПр was still set.  The hardware forces it on at the vector, so this is the
// assertion that the gate opens it again before dispatching -- v7's spl0()-on-entry, without which
// a system call runs to completion with the clock stopped.
//
// usys.ini asserts ACC == 0.  A nonzero ACC names the failing check -- see the F_* bits.

// clang-format off
#include "sys/types.h"
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/dir.h"
#include "sys/user.h"
#include "sys/proc.h"
#include "sys/text.h"
#include "sys/reg.h"
#include "sys/seg.h"
#include "sys/besm6dev.h"
// clang-format on

#include <besm6.h>

// `u' is NOT defined here, unlike in mmutest/uintr/utrap: crt0s.S reserves it, because the real
// syscall() finds its frame at u.u_stack and that stack needs room to grow.  maxmem is what
// utab.o wants.
int maxmem = 512 * 1024;

static struct proc pr;
static struct text tx;

// crt0s.S
extern unsigned uprogadr; // uprog's link-time word address, as a plain integer
extern int *ustkbase;     // base of the gate's stack; main() points it at u.u_stack
void gouser(unsigned uentry);
void halt(unsigned mask);

// brz.s
void drainbrz(void);

// Must match the EQUs in crt0s.S.
#define R13V   054321U // forged r13
#define USPV   070010U // forged r15, and the physical stack-switch sentinel
#define KSENT  0333333U
#define ARG1   01111
#define ARG2   02222
#define ARG3   03333
#define SENTA  07654

#define IMAGEPG 16 // physical page of the process image (data + stack), free memory

// Physical base of the data region == the physical address of virtual page 2.
#define DBASE (IMAGEPG * PGSZ + USIZE)

// The report slots, as offsets from DBASE (crt0s.S names them as virtual addresses).
#define S_ACC1 0
#define S_ERR1 1
#define S_VAL2 2
#define S_R13  3
#define S_R15A 4
#define S_ACC2 5
#define S_ERR2 6
#define S_R15B 7
#define S_ACC3 8
#define S_ERR3 9
#define S_ACC4 10
#define S_R15C 11

// What the stub syscalls hand back, so the checks below have something distinctive to see.
#define VAL1 012345
#define VAL2 067654

// Fault-mask bits, reported in the accumulator by halt().  Zero means every check passed.
#define F_STACK  0000001 // the stack was not switched: the frame landed on the user r15
#define F_NCALL  0000002 // the wrong syscalls arrived, or the wrong number of them
#define F_ACC1   0000004 // leg 1: r_val1 did not reach the accumulator
#define F_VAL2   0000010 // leg 1: r_val2 did not reach r12
#define F_ERR1   0000020 // leg 1: r14 is not 0 on success
#define F_R13    0000040 // leg 1: r13 was clobbered -- it is the caller's return address
#define F_R15A   0000100 // leg 1: r15 moved, though nothing was pushed
#define F_ARGS   0000200 // leg 2: the arguments arrived in the wrong order or wrong values
#define F_R15B   0000400 // leg 2: r15 was not popped by n-1
#define F_ERR2   0001000 // leg 2: the 3-argument call reported an error
#define F_RANGE  0002000 // leg 3: an out-of-range number was masked instead of range-checked
#define F_ERR3   0004000 // leg 3: not EINVAL, so nosys() did not run
#define F_BADEXT 0010000 // leg 4: badextr() did not signal SIGINS exactly once
#define F_ACC4   0020000 // leg 4: badext disturbed the machine
#define F_R15C   0040000 // leg 4: badext moved r15
#define F_IPL    0100000 // a door dispatched with БлПр still set: the level was never opened

static unsigned mask;

// What the stubs recorded.
static int ncall;                // how many sysent handlers ran
static int lastcall;             // the number of the last one
static int sawarg[3];            // leg 2's arguments, in the order the callee saw them
static int nsig, lastsig;        // psignal() from badextr()
static int sawpsw;               // PSW as every dispatched handler saw it, OR-ed together

// -------------------------------------------------------------------------
// The environment kernel/syscall.c needs.
// -------------------------------------------------------------------------

// Leg 1's handler: a getpid-shaped call.  Both results are set, so the frame's accumulator and
// r12 both have something to carry.
static void stub_two(void)
{
    ncall++;
    sawpsw |= __besm6_getpsw();
    lastcall     = 20;
    u.u_r.r_val1 = VAL1;
    u.u_r.r_val2 = VAL2;
}

// Leg 2's handler: a read-shaped call.  It reads its arguments the way every real v7 callee
// does -- as an ASCENDING array through u.u_ap, in prototype order -- which is the whole point
// of the marshalling under test.
static void stub_three(void)
{
    register struct a {
        int a1;
        int a2;
        int a3;
    } *uap = (struct a *)u.u_ap;

    ncall++;
    sawpsw |= __besm6_getpsw();
    lastcall     = 3;
    sawarg[0]    = uap->a1;
    sawarg[1]    = uap->a2;
    sawarg[2]    = uap->a3;
    u.u_r.r_val1 = 0;
    u.u_r.r_val2 = 0;
}

static void stub_none(void)
{
    ncall++;
    sawpsw |= __besm6_getpsw();
    lastcall = -1;
}

// The real thing sets EINVAL; leg 3 depends on that, so it is the kernel's own text.
void nosys(void)
{
    ncall++;
    sawpsw |= __besm6_getpsw();
    lastcall  = 0;
    u.u_error = EINVAL;
}

// A sysent[] shaped like the kernel's in the two slots the test uses.  NSYSENT entries, so the
// range check has the same bound it has in the kernel.
struct sysent sysent[NSYSENT] = {
    { 0, 0, stub_none }, // 0
    { 0, 0, stub_none }, // 1
    { 0, 0, stub_none }, // 2
    { 3, 0, stub_three }, // 3 = read-shaped: THREE arguments
    { 0, 0, stub_none }, // 4
    { 0, 0, stub_none }, // 5
    { 0, 0, stub_none }, // 6
    { 0, 0, stub_none }, // 7
    { 0, 0, stub_none }, // 8
    { 0, 0, stub_none }, // 9
    { 0, 0, stub_none }, // 10
    { 0, 0, stub_none }, // 11
    { 0, 0, stub_none }, // 12
    { 0, 0, stub_none }, // 13
    { 0, 0, stub_none }, // 14
    { 0, 0, stub_none }, // 15
    { 0, 0, stub_none }, // 16
    { 0, 0, stub_none }, // 17
    { 0, 0, stub_none }, // 18
    { 0, 0, stub_none }, // 19
    { 0, 0, stub_two },  // 20 = getpid-shaped: NO arguments, TWO results
    // 21..63 default to { 0, 0, 0 } and are never dispatched by this test
};

// The scheduling and signal calls syscall()'s tail makes.  None of them can do anything
// meaningful without the rest of the kernel; what matters is that they are reached and that
// psignal() records what badextr() raised.
char runrun;
char curpri;

int save(label_t lbl)
{
    // the direct call always returns 0; there is no resume() in this test
    return (0);
}

int issig(void)
{
    return (0);
}

void psig(void)
{
}

int setpri(struct proc *pp)
{
    return (0);
}

void qswtch(void)
{
}

void psignal(struct proc *p, int sig)
{
    nsig++;
    sawpsw |= __besm6_getpsw();
    lastsig = sig;
}

void addupc(int pc, void *prof, int incr)
{
}

// -------------------------------------------------------------------------

// Reached from crt0s.S's `toreport' after uprog's closing data fault.  Everything uprog stored
// went through the map, so drain the write cache before reading it back physically.
void report(void)
{
    volatile int *s = (volatile int *)DBASE;

    drainbrz();

    // The gate switched the stack: otherwise the frame landed on the user's r15.
    if (*(volatile unsigned *)USPV != KSENT || *(volatile unsigned *)(USPV + 1) != KSENT)
        mask |= F_STACK;

    // Four legs, four handlers -- leg 3 lands in nosys().
    if (ncall != 3 || lastcall != 0)
        mask |= F_NCALL;

    // Leg 1: the two-value return.
    if (s[S_ACC1] != VAL1)
        mask |= F_ACC1;
    if (s[S_VAL2] != VAL2)
        mask |= F_VAL2;
    if (s[S_ERR1] != 0)
        mask |= F_ERR1;
    if (s[S_R13] != (int)R13V)
        mask |= F_R13;
    if (s[S_R15A] != (int)USPV)
        mask |= F_R15A;

    // Leg 2: prototype order, and the callee's stack cleanup.
    if (sawarg[0] != ARG1 || sawarg[1] != ARG2 || sawarg[2] != ARG3)
        mask |= F_ARGS;
    if (s[S_R15B] != (int)USPV - 2)
        mask |= F_R15B;
    if (s[S_ERR2] != 0)
        mask |= F_ERR2;

    // Leg 3: range-checked, not masked.
    if (s[S_ACC3] != -1)
        mask |= F_RANGE;
    if (s[S_ERR3] != EINVAL)
        mask |= F_ERR3;

    // Leg 4: badext signalled and left everything alone.
    if (nsig != 1 || lastsig != SIGINS)
        mask |= F_BADEXT;
    if (s[S_ACC4] != SENTA)
        mask |= F_ACC4;
    if (s[S_R15C] != (int)USPV - 2)
        mask |= F_R15C;

    // Every leg: the gate opened the interrupt level before it dispatched.  БлПр is forced on at
    // the vector, so a handler that sees it still set means the `vtm 3' in crt0s.S (kernel/besm6.S
    // over there) went missing -- and a syscall would run to completion with the clock stopped.
    // There is no C-visible shadow of this; it has to be read out of PSW.
    if (sawpsw & PSW_INTR_DISABLE)
        mask |= F_IPL;

    halt(mask); // never returns
}

int main()
{
    unsigned uaddr, uentry;

    // The gate's stack IS the u-area's tail, exactly as in the real kernel -- syscall() reads
    // u.u_stack for its frame, so the value the gate loads into M15 has to agree with it.
    // crt0s.S leaves the cell for us to fill because the offset is not known to the assembler.
    ustkbase = u.u_stack;

    // Build the user map: uprog's OWN physical page at virtual page 0 (so the forged user runs
    // the real code), text two pages, data two -- virtual pages 2-3 are where uprog reports --
    // and one stack page at USTKPAGE (virtual 28), which is where the forged r15 points so that
    // leg 2 can push arguments below it.  Virtual page 4 is left closed: that is uprog's exit.
    uaddr  = uprogadr;           // uprog's WORD address (a plain integer from the linker)
    uentry = uaddr & (PGSZ - 1); // its offset within virtual page 0

    tx.x_caddr = uaddr & ~(PGSZ - 1); // map virtual page 0 -> uprog's physical page
    tx.x_size  = 2 * PGSZ;

    pr.p_addr  = IMAGEPG * PGSZ;
    pr.p_size  = USIZE + 2 * PGSZ + PGSZ;
    pr.p_textp = &tx;

    u.u_procp = &pr;
    u.u_tsize = 2 * PGSZ;
    u.u_dsize = 2 * PGSZ;
    u.u_ssize = PGSZ;

    sureg();

    // The stack-switch sentinel: seed physical USPV/USPV+1.  If a gate failed to switch r15,
    // syscall()'s C frame would run on the user's r15 -- a physical index, БлП being forced on
    // -- and overwrite this.
    *(volatile unsigned *)USPV       = KSENT;
    *(volatile unsigned *)(USPV + 1) = KSENT;
    drainbrz();

    // Mask every interrupt source: the forged SPSW enters user mode with БлПр CLEAR, and the
    // interval timer re-arms GRP_TIMER at reset.  Nothing here wants an external interrupt.
    __besm6_mod(MOD_MGRP, 0);

    gouser(uentry); // forge the user context and enter it; never returns
    return 0;
}
