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
// clang-format on

#define USER 0200 /* user-mode flag added to dev */

/*
 * Word indices of the user's saved registers in the trap frame (reg.h).
 * regloc[0..14] are the general registers zeroed on exec (ACC..r14);
 * regloc[15] = r15 (SP) and regloc[16] = IRET (PC), both set explicitly.
 */
char regloc[] = {
    ACC, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, /* [0..14] */
    R15,                                                              /* [15] SP */
    IRET,                                                             /* [16] PC */
};

/*
 * Called from mch.s when a processor trap occurs.
 * The arguments are the words saved on the system stack
 * by the hardware and software during the trap processing.
 * Their order is dictated by the hardware and the details
 * of C's calling sequence. They are peculiar in that
 * this call is not 'by value' and changed user registers
 * get copied back on return.
 * dev is the kind of trap that occurred.
 */
void trap(struct trap tr)
{
    register int i = 0;
    register int *a;
    register struct sysent *callp;
    time_t syst;
    int osp;
    int dev; /* trap kind: TODO 15c decode from ГРП (read live) */

    syst    = u.u_stime;
    u.u_ar0 = &tr.acc;
    /*
     * TODO 15c: decode the fault kind from ГРП -- read live via
     * __besm6_mod(MOD_GRP,0), not from the frame (ГРП has no slot): bit 20 =
     * data protection (faulting page in bits 5-9) -> grow()/SIGSEG, bit 14 =
     * instruction protection, bit 13 = illegal instruction, bit 15 = check.
     * The 0500 gate (15c) builds the frame and drives this decode; until then
     * there is no trap kind, so the switch runs on an inert stub.
     */
    dev = 0;
    if (USERMODE(tr.spsw))
        dev |= USER;
    switch (minor(dev)) {
    /*
     * Trap not expected.
     * Usually a kernel mode bus error.
     */
    default:
        printf("acc=%o r13=%o r14=%o r15=%o\n", u.u_ar0[ACC], u.u_ar0[R13], u.u_ar0[R14],
               u.u_ar0[R15]);
        printf("iret=%o eret=%o spsw=%o\n", u.u_ar0[IRET], u.u_ar0[ERET], u.u_ar0[SPSW]);
        printf("R=%o RMR=%o C=%o\n", u.u_ar0[RREG], u.u_ar0[RMR], u.u_ar0[CREG]);
        panic("trap");

    case 0 + USER: /* divide error */
    case 4 + USER: /* overflow exception */
    case 5 + USER: /* bound range exceeded */
        i = SIGFPT;
        break;

    case 1 + USER: /* debug exception */
    case 3 + USER: /* breakpoint exception */
        i = SIGTRC;
        /* TODO 15c: single-step is the address-break registers М034/М035, not a
         * flag bit; there is no EFL/TBIT to clear. */
        break;

    case 6 + USER: /* invalid opcode */
    case 9 + USER: /* coprocessor segment overrun */
        i = SIGINS;
        break;

    case 7 + USER:                  /* device not available */
        panic("fpu not available"); /* XXX */
        break;

    case 8 + USER:  /* double fault */
    case 10 + USER: /* invalid tss */
    case 11 + USER: /* segment not present */
    case 12 + USER: /* stack fault exception */
    case 13 + USER: /* general protection exception */
        i = SIGBUS;
        break;

    /*
     * If the user SP is below the stack segment,
     * grow the stack automatically.
     */
    case 14 + USER: /* page fault */
        osp = tr.r15;
        if (grow((unsigned)osp))
            goto out;
        i = SIGSEG;
        break;

    /*
     * Allow process switch
     */
    case 15 + USER:
        goto out;

    /*
     * Since the floating exception is an
     * imprecise trap, a user generated
     * trap may actually come from kernel
     * mode. In this case, a signal is sent
     * to the current process to be picked
     * up later.
     */
    case 16: /* floating point error */
        psignal(u.u_procp, SIGFPT);
        return;

    case 16 + USER:
        i = SIGFPT;
        break;

    case 48 + USER: /* sys call */
        u.u_error = 0;
        /*
         * TODO 15d: BESM-6 syscall ABI (cmd/sim/syscall.cpp) -- number from the
         * `$77 N' operand (r14), last arg in ACC and the rest below r15, result
         * in ACC and errno in r14 (0 on success).  No carry/EBIT.  The arg fetch
         * and number decode below are x86-shaped placeholders kept only so the
         * tree builds; 15d rewrites them.
         */
        a     = (int *)tr.r15;              /* TODO 15d: real arg staging */
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
        addupc(tr.iret, &u.u_prof, (int)(u.u_stime - syst));
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
