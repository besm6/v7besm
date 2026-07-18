/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 1999 Robert Nordier. All rights reserved. */

// clang-format off
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/dir.h"
#include "sys/user.h"
#include "sys/proc.h"
#include "sys/reg.h"
#include "sys/seg.h"
#include "sys/besm6dev.h"
// clang-format on

#include <besm6.h>

#define USER 0200 /* user-mode flag added to dev */

/*
 * The kinds of trap this kernel dispatches.  These are OURS, not hardware numbers: the
 * BESM-6 has one internal-interrupt vector (0500) and reports the cause in ГРП, so unlike
 * the x86 there is no vector number to switch on and the gate passes none.  trap() reads
 * ГРП live -- the fault bits are not framed (reg.h) -- and folds it to one of these.
 *
 * T_SYSCALL is not reachable through 0500 at all: the extracode gate at 0577 is its own
 * door (besm6.S: syscall), and wiring it to the case below is task 15d.
 */
#define T_DATA    1  /* data protection: touched a closed page */
#define T_INSN    2  /* instruction protection: fetched from a closed page */
#define T_ILL     3  /* privileged instruction attempted in user mode */
#define T_CHECK   4  /* instruction check: the word fetched is not an instruction */
#define T_BREAK   5  /* address-break match (М034/М035) */
#define T_SYSCALL 48 /* extracode э77 -- TODO 15d */

/*
 * Word indices of the user's saved registers in the trap frame (reg.h).
 * regloc[0..14] are the general registers zeroed on exec (ACC..r14);
 * regloc[15] = r15 (SP) and regloc[16] = RET (PC), both set explicitly.
 */
char regloc[] = {
    ACC, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, /* [0..14] */
    R15,                                                              /* [15] SP */
    RET,                                                              /* [16] PC */
};

/*
 * Called from the 0500 gate in besm6.S when the machine takes an internal interrupt --
 * a protection violation, an illegal instruction, an instruction check.
 *
 * The gate passes nothing.  Its stack switch is unconditional, so this door never nests
 * and the reg.h frame it built is always at the base of the kernel stack -- the link-time
 * constant u.u_stack, which is what machdep.c's `ustkbase' holds and what the gate loaded
 * into M15 before filling the frame.
 * `tr' is that frame IN PLACE, not a copy: u.u_ar0 aliases it too, so a register this
 * function or psig() changes is a register the gate's epilogue reloads on the way out.
 *
 * The cause is not in the frame -- the machine reports it in ГРП, which we read live below.
 */
void trap(void)
{
    register struct trap *tr = (struct trap *)u.u_stack;
    register int i           = 0;
    register int *a;
    register struct sysent *callp;
    time_t syst;
    unsigned grp;  /* ГРП, read live: the fault cause */
    unsigned page; /* the faulting virtual page (data violations only) */
    int dev;       /* the trap kind, plus USER if it came from user mode */

    syst    = u.u_stime;
    u.u_ar0 = (int *)tr;

    /*
     * The restart protocol (doc/Memory_Mapping.md).  Before vectoring, the machine took
     * delivery of the NEXT WORD of the instruction stream and saved that as the return
     * address, setting SPSW_NEXT_RK to say so -- so the framed RET is the faulting word
     * plus one, and `выпр' would resume past the instruction that faulted.  Back it up
     * here, unconditionally, so that everything downstream (grow()'s retry, sendsig(),
     * ptrace, addupc()) sees the frame describing the FAULTING instruction.
     *
     * Only the word needs correcting: SPSW_RIGHT_INSTR already says WHICH HALF of it
     * faulted, and `выпр' reloads the half-word indicator from it -- verified on the
     * machine from both halves (kernel/test/utrap).  Faults that do not advance the PC
     * (an illegal instruction, an instruction check) leave SPSW_NEXT_RK clear, and the
     * guard makes this a no-op for them.
     */
    if (tr->spsw & SPSW_NEXT_RK) {
        tr->ret--;
        tr->spsw &= ~SPSW_NEXT_RK;
    }

    /*
     * Decode the cause, and dismiss it: MOD_GRPCLR clears the bits that are ZERO in the
     * accumulator, so a bit left standing could fire afterwards as a spurious external
     * interrupt if it happens to be unmasked in МГРП.
     */
    grp  = __besm6_mod(MOD_GRP, 0);
    page = 0;
    if (grp & GRP_OPRND_PROT) {
        dev  = T_DATA;
        page = (grp & GRP_PAGE_MASK) >> GRP_PAGE_SHIFT;
        __besm6_mod(MOD_GRPCLR, ~GRP_OPRND_PROT);
    } else if (grp & GRP_INSN_PROT) {
        dev = T_INSN; /* no page is reported for an instruction fault */
        __besm6_mod(MOD_GRPCLR, ~GRP_INSN_PROT);
    } else if (grp & GRP_ILL_INSN) {
        dev = T_ILL;
        __besm6_mod(MOD_GRPCLR, ~GRP_ILL_INSN);
    } else if (grp & GRP_INSN_CHECK) {
        dev = T_CHECK;
        __besm6_mod(MOD_GRPCLR, ~GRP_INSN_CHECK);
    } else if (grp & GRP_BREAKPOINT) {
        dev = T_BREAK;
        __besm6_mod(MOD_GRPCLR, ~GRP_BREAKPOINT);
    } else {
        dev = 0; /* vectored with nothing pending: falls to the panic below */
    }
    if (USERMODE(tr->spsw))
        dev |= USER;

    switch (minor(dev)) {
    /*
     * Trap not expected.  Either a fault taken in kernel mode -- the user-access
     * family validates its range up front and has no recovery hook, so that is a
     * kernel bug -- or a cause we do not decode.
     */
    default:
        printf("acc=%o r13=%o r14=%o r15=%o\n", u.u_ar0[ACC], u.u_ar0[R13], u.u_ar0[R14],
               u.u_ar0[R15]);
        printf("ret=%o spsw=%o grp=%o page=%o\n", u.u_ar0[RET], u.u_ar0[SPSW], grp, page);
        printf("R=%o RMR=%o C=%o\n", u.u_ar0[RREG], u.u_ar0[RMR], u.u_ar0[CREG]);
        panic("trap");

    /*
     * Data protection: the user touched a page that is closed to data.  If the page is
     * the one just below the stack, grow the stack automatically and retry; the frame's
     * RET already points back at the faulting instruction (the restart protocol above).
     *
     * TODO 17: grow() still takes a word address and still assumes the x86's
     * downward-growing stack, so the faulting PAGE is converted back to an address here.
     * When 17 flips the stack direction, grow() takes the page number and this line goes.
     */
    case T_DATA + USER:
        if (grow(page << PGSH))
            goto out;
        i = SIGSEG;
        break;

    case T_INSN + USER: /* instruction fetch from a closed page */
        i = SIGSEG;
        break;

    case T_ILL + USER:   /* a privileged instruction in user mode */
    case T_CHECK + USER: /* the fetched word is not an instruction */
        i = SIGINS;
        break;

    case T_BREAK + USER: /* address-break match */
        i = SIGTRC;
        /* TODO 17: single-step is the address-break registers М034/М035, not a flag
         * bit; there is no EFL/TBIT to clear, so re-arming belongs to procxmt(). */
        break;

    case T_SYSCALL + USER: /* sys call */
        u.u_error = 0;
        /*
         * TODO 15d: BESM-6 syscall ABI (cmd/sim/syscall.cpp) -- number from the
         * `$77 N' operand (r14), last arg in ACC and the rest below r15, result
         * in ACC and errno in r14 (0 on success).  No carry/EBIT.  The arg fetch
         * and number decode below are x86-shaped placeholders kept only so the
         * tree builds; 15d rewrites them.
         */
        a     = (int *)tr->r15;             /* TODO 15d: real arg staging */
        callp = &sysent[u.u_ar0[R14] & 077]; /* TODO 15d: number from $77 N */
        for (i = 0; i < callp->sy_narg; i++)
            u.u_arg[i] = fuword((caddr_t)a++);
        u.u_dirp     = (caddr_t)u.u_arg[0];
        u.u_r.r_val1 = 0;
        u.u_r.r_val2 = 0;
        u.u_ap       = u.u_arg;
        if (save(u.u_qsav)) {
            if (u.u_error == 0)
                u.u_error = EINTR;
        } else {
            (*callp->sy_call)();
        }
        if (u.u_error) {
            u.u_ar0[R_ERRNO] = u.u_error; /* errno in r14, no carry bit */
        } else {
            u.u_ar0[ACC]     = u.u_r.r_val1;
            u.u_ar0[R_VAL2]  = u.u_r.r_val2; /* TODO 15d: 2nd-result slot */
            u.u_ar0[R_ERRNO] = 0;
        }
        goto out;
    }
    /*
     * If you run elaborate /bin/sh scripts, you'll
     * probably want to disable the following line.
     */
    printf("** SIGNAL %d **\n", i);
    psignal(u.u_procp, i);

out:
    if (issig()) {
        psig();
    }
    curpri = setpri(u.u_procp);
    if (runrun)
        qswtch();
    if (u.u_prof.pr_scale)
        addupc(tr->ret, &u.u_prof, (int)(u.u_stime - syst));
}

/*
 * stray interrupt
 */
void stray(int dev)
{
    printf("stray interrupt %d ignored\n", dev & ~0x20);
}

/*
 * nonexistent system call-- set fatal error code.
 */
void nosys()
{
    u.u_error = EINVAL;
}

/*
 * Ignored system call
 */
void nullsys()
{
}
