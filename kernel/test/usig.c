// usig -- signal delivery on the machine: the frame kernel/sendsig.c builds, the handler it
// enters, and the return through the word it plants on the user stack.  lib/README.md phase 6.
//
// The fifth forge test, and the second that LINKS THE CODE UNDER TEST rather than a copy of it:
// kernel/sendsig.c is its own file exactly so this test can pull in the real sendsig() and the
// real sigret(), the way usys pulls in the real syscall().  What is hand-built here is only the
// environment they need -- a sysent[] with the real sigret() at row 45, a psig() that delivers,
// a trap() that delivers, and stubs for grow() and exit().
//
// Two legs, one per door, because a signal is delivered at both:
//
//   1. AT A SYSCALL RETURN.  uprog issues `$77 20'; the stub sets both results and arms the
//      delivery, syscall() writes the results into the frame, and sysret()'s psig() calls
//      sendsig().  So the frame that goes on the user stack carries that call's own results,
//      and the handler then clobbers the accumulator, r9, r11, r12 and r14 on purpose.  Getting
//      all five back is the proof.
//
//   2. AT A FAULT RETURN, FROM A RIGHT-HALF INSTRUCTION.  This is the leg no user-mode return
//      path could have handled: which half of a word to resume at lives in SPSW, and only
//      `выпр' reloads it.  uprog's faulting `xta' is the RIGHT half of a word whose LEFT half
//      bumps a counter; if the round trip lost SPSW_RIGHT_INSTR the retry would run the left
//      half again, so the counter -- read out of the second fault's frame -- must be exactly 1.
//
// And underneath both: THE PLANTED WORD IS EXECUTED.  It is a word the kernel stored into a data
// page and the user jumps to; the machine tags stored words with a свертка and checks the tag on
// instruction fetch, so only running it on the machine proves the tag is right.  If it were not,
// the handler's `13 uj' would raise an instruction check rather than reaching the kernel.
//
// usig.ini asserts ACC == 0.  A nonzero ACC names the failing check -- see the F_* bits.

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

// `u' is NOT defined here: crt0sg.S reserves it, because the real syscall() and the real
// sendsig() both find the frame at u.u_stack and that stack needs room to grow.
int maxmem = 512 * 1024;

static struct proc pr;
static struct text tx;

// crt0sg.S
extern unsigned uprogadr, handler1adr, handler2adr;
extern int *ustkbase;
void gouser(unsigned uentry);
void halt(unsigned mask);

// brz.s
void drainbrz(void);

// Must match the EQUs in crt0sg.S.
#define R13V    054321U
#define USPV    070010U
#define KSENT   0333333U
#define R9V     01234
#define R11V    02345
#define CLOBBER 0777

#define IMAGEPG 16 // physical page of the process image (data + stack), free memory

// Physical base of the data region == the physical address of virtual page 2.
#define DBASE (IMAGEPG * PGSZ + USIZE)

// The report slots, as offsets from DBASE (crt0sg.S names them as virtual addresses).
#define S_ACC  0
#define S_ERR  1
#define S_VAL2 2
#define S_R13  3
#define S_R15  4
#define S_R9   5
#define S_R11  6
#define S_SIG1 8
#define S_HR14 9
#define S_HR13 10
#define S_HR15 11
#define S_SIG2 12

// What the stub syscall hands back.
#define VAL1 012345
#define VAL2 067654

// The two signals the legs deliver.  Any two distinct numbers would do; these are the ones a
// user program actually sees.
#define SIG1 SIGTRM
#define SIG2 SIGINT

// Fault-mask bits, reported in the accumulator by halt().  Zero means every check passed.
#define F_NCALL  0000001  // the wrong syscalls arrived, or the wrong number of them
#define F_SIG1   0000002  // leg 1: the handler ran with the wrong signal number, or not at all
#define F_HR14   0000004  // leg 1: r14 at handler entry was not -1 (the argument count)
#define F_HR13   0000010  // leg 1: r13 did not name the planted word just above the frame
#define F_HR15   0000020  // leg 1: r15 was not one above that
#define F_ACC    0000040  // leg 1: the accumulator did not come back (r_val1)
#define F_VAL2   0000100  // leg 1: r12 did not come back (r_val2)
#define F_ERR    0000200  // leg 1: r14 did not come back (0 on success)
#define F_R13    0000400  // leg 1: r13 was clobbered -- the caller's return address
#define F_R15    0001000  // leg 1: r15 did not come back, so the frame was not popped
#define F_R9     0002000  // leg 1: r9 was not restored
#define F_R11    0004000  // leg 1: r11 was not restored
#define F_NFAULT 0010000  // leg 2: the wrong number of faults
#define F_HALF   0020000  // leg 2: the fault was not taken from the right half of the word
#define F_RETRY  0040000  // leg 2: sigret() resumed at the wrong half -- the counter is not 1
#define F_RESUME 0100000  // leg 2: the second fault was not at the same instruction
#define F_SIG2   0200000  // leg 2: the fault leg's handler did not run
#define F_KTRAP  0400000  // a fault arrived claiming to be from supervisor mode
#define F_EXIT   01000000 // sendsig() could not build the frame and called exit()

static unsigned mask;

// What the stubs recorded.
static int ncall;   // how many sysent handlers ran
static int deliver; // set by the leg-1 stub, consumed by issig()/psig()
static int nfault;  // how many faults trap() saw
static int fault1spsw, fault2spsw;
static int fault1ret, fault2ret;
static int fault2r11; // the left-half counter, read out of the second fault's frame

static unsigned hentry1, hentry2; // the handlers' VIRTUAL addresses

static void report(void);

// -------------------------------------------------------------------------
// The environment kernel/syscall.c and kernel/sendsig.c need.
// -------------------------------------------------------------------------

// Leg 1's handler: a getpid-shaped call.  Both results are set, so the frame has something
// distinctive to carry through the delivery, and the delivery is armed for sysret() to find --
// which is the real sequence: syscall() writes the results into the frame FIRST and calls
// psig() after, so what sendsig() copies out is a complete return.
static void stub_two(void)
{
    ncall++;
    u.u_r.r_val1 = VAL1;
    u.u_r.r_val2 = VAL2;
    deliver      = 1;
}

static void stub_none(void)
{
    ncall++;
}

void nosys(void)
{
    ncall++;
    u.u_error = EINVAL;
}

// sysent[] with the real sigret() at row 45, where the kernel keeps it -- the planted word
// issues `$77 45' and must find it.
struct sysent sysent[NSYSENT] = {
    { 0, 0, stub_none }, // 0
    { 0, 0, stub_none }, // 1
    { 0, 0, stub_none }, // 2
    { 0, 0, stub_none }, // 3
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
    { 0, 0, stub_two },  // 20 = getpid-shaped: no arguments, two results, arms the delivery
    { 0, 0, stub_none }, // 21
    { 0, 0, stub_none }, // 22
    { 0, 0, stub_none }, // 23
    { 0, 0, stub_none }, // 24
    { 0, 0, stub_none }, // 25
    { 0, 0, stub_none }, // 26
    { 0, 0, stub_none }, // 27
    { 0, 0, stub_none }, // 28
    { 0, 0, stub_none }, // 29
    { 0, 0, stub_none }, // 30
    { 0, 0, stub_none }, // 31
    { 0, 0, stub_none }, // 32
    { 0, 0, stub_none }, // 33
    { 0, 0, stub_none }, // 34
    { 0, 0, stub_none }, // 35
    { 0, 0, stub_none }, // 36
    { 0, 0, stub_none }, // 37
    { 0, 0, stub_none }, // 38
    { 0, 0, stub_none }, // 39
    { 0, 0, stub_none }, // 40
    { 0, 0, stub_none }, // 41
    { 0, 0, stub_none }, // 42
    { 0, 0, stub_none }, // 43
    { 0, 0, stub_none }, // 44
    { 0, 0, sigret },    // 45 = sigreturn -- THE REAL ONE (kernel/sendsig.c)
    // 46..63 default to { 0, 0, 0 } and are never dispatched by this test
};

// The scheduling and signal calls syscall()'s tail makes.  issig()/psig() are where a signal is
// delivered for real, so here they are the leg-1 delivery: the stub arms it, sysret() finds it.
char runrun;
char curpri;

int save(label_t lbl)
{
    return (0);
}

int issig(void)
{
    return (deliver);
}

void psig(void)
{
    deliver = 0;
    sendsig((caddr_t)hentry1, SIG1);
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
}

void addupc(int pc, void *prof, int incr)
{
}

// sendsig() grows the stack for the frame.  Here the stack page is mapped from the start, so
// there is nothing to do -- and nothing to test: grow() is sig.c's, and ugrow tests it.
int grow(int pg)
{
    return (0);
}

// sendsig() calls this if the frame could not be written; it must not happen here.
void exit(int status)
{
    mask |= F_EXIT;
    report();
}

// The gate's supervisor arm.  Nothing in this test faults in kernel mode.
void ktrap(void)
{
    mask |= F_KTRAP;
    report();
}

// -------------------------------------------------------------------------

// The fault door's C side: kernel/trap.c cut down to the two things this test needs -- the
// restart fixup, and a delivery.  trap.c itself cannot be linked (it drags in printf, the signal
// machinery and grow(), and with them the rest of the kernel), which is why this is a copy.
void trap(void)
{
    register struct trap *tr = (struct trap *)u.u_stack;

    u.u_ar0 = (int *)tr;

    // The restart protocol (doc/Memory_Mapping.md): the machine vectored with the PC already
    // past the faulting instruction.  Back it up, exactly as trap() does, so the frame -- and
    // through it sendsig() and sigret() -- describes the FAULTING instruction.
    if (tr->spsw & SPSW_NEXT_RK) {
        tr->ret--;
        tr->spsw &= ~SPSW_NEXT_RK;
    }
    __besm6_mod(MOD_GRPCLR, ~GRP_OPRND_PROT);

    nfault++;
    if (nfault == 1) {
        // The first fault: deliver.  The handler returns through the planted word and sigret()
        // puts the program back on the faulting instruction -- the RIGHT half of its word.
        fault1spsw = tr->spsw;
        fault1ret  = tr->ret;
        sendsig((caddr_t)hentry2, SIG2);
        return;
    }

    // The second fault is that same instruction, re-executed.  r11 is in the frame: it counts
    // how many times the word's LEFT half ran, and one is the only right answer.
    fault2spsw = tr->spsw;
    fault2ret  = tr->ret;
    fault2r11  = tr->r11;
    report(); // never returns
}

// Reached from trap() on the second fault.  Everything uprog and the handlers stored went
// through the map, so drain the write cache before reading it back physically.
static void report(void)
{
    volatile int *s = (volatile int *)DBASE;

    drainbrz();

    // One syscall stub ran: `$77 20'.  The planted `$77 45' lands in sigret(), which is not a
    // stub and does not count itself.
    if (ncall != 1)
        mask |= F_NCALL;

    // Leg 1, at the handler: the entry ABI.  The number is the one argument and travels in the
    // accumulator; r14 is the negative argument count; r13 names the planted word, which sits
    // one word above the 21-word frame, i.e. at the user r15 sendsig() was given.
    if (s[S_SIG1] != SIG1)
        mask |= F_SIG1;
    // r14 is an INDEX REGISTER: sendsig() frames a full-width -1, `intret' loads its low 15 bits,
    // and the handler's `ita' zero-extends what it finds -- so what lands here is 077777, exactly
    // as `14 vtm -1' would have left it.  That IS the negative argument count on this machine.
    if (s[S_HR14] != 077777)
        mask |= F_HR14;
    if (s[S_HR13] != (int)USPV + NREGFRAME)
        mask |= F_HR13;
    if (s[S_HR15] != (int)USPV + NREGFRAME + 1)
        mask |= F_HR15;

    // Leg 1, after the round trip: everything the handler wrecked is back.
    if (s[S_ACC] != VAL1)
        mask |= F_ACC;
    if (s[S_VAL2] != VAL2)
        mask |= F_VAL2;
    if (s[S_ERR] != 0)
        mask |= F_ERR;
    if (s[S_R13] != (int)R13V)
        mask |= F_R13;
    if (s[S_R15] != (int)USPV)
        mask |= F_R15;
    if (s[S_R9] != R9V)
        mask |= F_R9;
    if (s[S_R11] != R11V)
        mask |= F_R11;

    // Leg 2: the fault leg.  Both faults came from the RIGHT half of the word (the frame says
    // so), they were the same instruction, and the word's left half ran exactly once.
    if (nfault != 2)
        mask |= F_NFAULT;
    if (!(fault1spsw & SPSW_RIGHT_INSTR) || !(fault2spsw & SPSW_RIGHT_INSTR))
        mask |= F_HALF;
    if (fault2ret != fault1ret)
        mask |= F_RESUME;
    if (fault2r11 != 1)
        mask |= F_RETRY;
    if (s[S_SIG2] != SIG2)
        mask |= F_SIG2;

    halt(mask); // never returns
}

int main()
{
    unsigned uaddr, ubase, uentry;

    // The gates' stack IS the u-area's tail, as in the real kernel: syscall(), trap() and
    // sendsig() all read u.u_stack for the frame.
    ustkbase = u.u_stack;

    // Build the user map, as usys does: uprog's own physical page at virtual page 0, text two
    // pages, data two (virtual 2-3, where uprog and the handlers report), one stack page at
    // USTKPAGE (virtual 28), where the forged r15 points and where the signal frame is built.
    // Virtual page 4 is left closed: that is leg 2's fault.
    uaddr  = uprogadr;
    ubase  = uaddr & ~(PGSZ - 1);
    uentry = uaddr - ubase;

    // The handlers live in the same text, so their virtual addresses are their offsets from the
    // same base.  This is what sendsig() is given, and what `выпр' enters.
    hentry1 = handler1adr - ubase;
    hentry2 = handler2adr - ubase;

    tx.x_caddr = ubase;
    tx.x_size  = 2 * PGSZ;

    pr.p_addr  = IMAGEPG * PGSZ;
    pr.p_size  = USIZE + 2 * PGSZ + PGSZ;
    pr.p_textp = &tx;

    u.u_procp = &pr;
    u.u_tsize = 2 * PGSZ;
    u.u_dsize = 2 * PGSZ;
    u.u_ssize = PGSZ;

    sureg();

    // The stack-switch sentinel, as in usys: if a gate failed to switch r15, a C frame would run
    // on the user's r15 -- a physical index, БлП being forced on -- and overwrite this.  Here it
    // would also collide with the signal frame, which is built at exactly USPV.
    *(volatile unsigned *)USPV       = KSENT;
    *(volatile unsigned *)(USPV + 1) = KSENT;
    drainbrz();

    // Mask every interrupt source: the forged SPSW enters user mode with БлПр CLEAR, and the
    // interval timer re-arms GRP_TIMER at reset.
    __besm6_mod(MOD_MGRP, 0);

    gouser(uentry); // forge the user context and enter it; never returns
    return 0;
}
