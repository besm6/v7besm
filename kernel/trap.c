/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

// clang-format off
#include "sys/types.h"
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

/*
 * THE ГРП BIT IS THE TRAP KIND.  The BESM-6 has one internal-interrupt vector (0500) and
 * reports the cause in ГРП, so there is no vector number to switch on and the gate passes
 * none; trap() reads ГРП live -- the fault bits are not framed (reg.h) -- and dispatches on
 * the bit directly.  v7 folds its vector numbers into a T_* enumeration first, because there
 * the hardware hands over a number that means nothing to the kernel.  Here it hands over the
 * cause itself, and an enumeration in between would name the same five things twice.
 *
 * There is no trap kind for a system call, either: an extracode is not reachable through 0500
 * at all.  The hardware vectors э50-э77 straight to 0550-0577, so the syscall gate is its own
 * door (besm6.S: `sysgate'/`badext') and its C side is kernel/syscall.c.
 */

/* Every ГРП bit that vectors through 0500 -- the five trap() decodes. */
#define GRP_FAULTS (GRP_OPRND_PROT | GRP_INSN_PROT | GRP_ILL_INSN | GRP_INSN_CHECK | GRP_BREAKPOINT)

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
 * The register dump both panic paths print.  `grp' is passed rather than re-read because
 * each caller has already dismissed the bit it decoded.
 *
 * The faulting page is derived here, not by the caller, so that the rule -- ГРП's bits 5-9
 * mean a page ONLY for a data violation, and are stale for every other cause -- is written
 * once and both dumps obey it.
 */
static void dumpregs(struct trap *tr, unsigned grp)
{
    unsigned page = (grp & GRP_OPRND_PROT) ? (grp & GRP_PAGE_MASK) >> GRP_PAGE_SHIFT : 0;

    printf("acc=%o r13=%o r14=%o r15=%o\n", tr->acc, tr->r13, tr->r14, tr->r15);
    printf("ret=%o spsw=%o grp=%o page=%o\n", tr->ret, tr->spsw, grp, page);
    printf("R=%o RMR=%o C=%o\n", tr->rreg, tr->rmr, tr->creg);
}

/*
 * A fault taken in SUPERVISOR mode: a kernel bug.  Called from the 0500 gate in besm6.S,
 * which branches straight here -- `u1a ktrap', not a call -- when СПСВ says the interrupted
 * context was the kernel.  NEVER RETURNS, which is what lets the gate branch: there is no
 * return address, no `intret' behind it and no frame to restore.
 *
 * The user-access family validates its range up front with useracc() and has no `nofault'
 * path (usermem.S), so there is nothing here to recover; all this owes anyone is a legible
 * dump.  It runs with БлПр still set, exactly as the hardware left it -- the gate does not
 * open the level on this path (see the header over trapgate) -- and it does not need it
 * open: putchar() is polled output held at spl7 (dev/sc.c).
 *
 * It does NOT set u.u_ar0.  That name means the USER's saved registers, and this frame is a
 * kernel context; on a fault taken inside a syscall, publishing it would destroy the frame
 * that syscall's psig()/sendsig() still write through -- and panic() below does not stop the
 * machine before that matters, since update() sleeps and other processes run on.
 */
void ktrap(void)
{
    struct trap *tr = (struct trap *)u.u_stack;
    unsigned grp;

    /* Back the PC up (see trap() below), so the dump names the FAULTING instruction. */
    if (tr->spsw & SPSW_NEXT_RK) {
        tr->ret--;
        tr->spsw &= ~SPSW_NEXT_RK;
    }

    /*
     * Read the cause, then dismiss EVERY fault bit -- not just the one that fired.  We are
     * not returning to the faulting context, but we are not stopping the machine either:
     * panic() calls update(), update() sleeps, and swtch() runs the other processes.  A
     * fault bit left standing in ГРП would be read live by the next process's trap() and
     * shadow its real cause, the decode being a priority-ordered if/else.
     */
    grp = __besm6_mod(MOD_GRP, 0);
    __besm6_mod(MOD_GRPCLR, ~GRP_FAULTS);

    dumpregs(tr, grp);
    panic("kernel trap");
}

/*
 * A fault taken in USER mode.  Called from the 0500 gate in besm6.S when the machine takes
 * an internal interrupt -- a protection violation, an illegal instruction, an instruction
 * check.  A fault from supervisor never arrives here; the gate sends it to ktrap() above.
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
    time_t syst;
    unsigned grp; /* ГРП, read live: the fault cause */

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
     * Decode the cause, dismiss it, and pick the signal -- one arm per ГРП bit, in priority
     * order.  There is no intermediate "trap kind" to switch on a second time: the machine
     * reports the cause as a bit, and a bit is what this dispatches on.
     *
     * Dismissal is MOD_GRPCLR, which clears the bits that are ZERO in the accumulator (hence
     * the `~' spelling), and it comes first in each arm: a bit left standing could fire
     * afterwards as a spurious external interrupt if it happens to be unmasked in МГРП.
     */
    grp = __besm6_mod(MOD_GRP, 0);
    if (grp & GRP_OPRND_PROT) {
        /*
         * Data protection: the user touched a page that is closed to data.  If it is the page
         * just above the stack, grow the stack automatically and retry -- the frame's RET
         * already points back at the faulting instruction (the restart protocol above).
         *
         * grow() takes the page as reported.  A page number is all the machine gives us (ГРП
         * bits 5-9), and with the stack growing up from USTKPAGE that is exactly what it needs.
         */
        __besm6_mod(MOD_GRPCLR, ~GRP_OPRND_PROT);
        if (grow((grp & GRP_PAGE_MASK) >> GRP_PAGE_SHIFT))
            goto out;
        i = SIGSEG;

    } else if (grp & GRP_INSN_PROT) { /* instruction fetch from a closed page */
        __besm6_mod(MOD_GRPCLR, ~GRP_INSN_PROT);
        i = SIGSEG;

    } else if (grp & GRP_ILL_INSN) { /* a privileged instruction in user mode */
        __besm6_mod(MOD_GRPCLR, ~GRP_ILL_INSN);
        i = SIGINS;

    } else if (grp & GRP_INSN_CHECK) { /* the word fetched is not an instruction */
        __besm6_mod(MOD_GRPCLR, ~GRP_INSN_CHECK);
        i = SIGINS;

    } else if (grp & GRP_BREAKPOINT) { /* address-break match (М034/М035) */
        __besm6_mod(MOD_GRPCLR, ~GRP_BREAKPOINT);
        i = SIGTRC;
        /* TODO 17: single-step is the address-break registers М034/М035, not a flag bit;
         * there is no EFL/TBIT to clear, so re-arming belongs to procxmt(). */

    } else {
        /* Vectored with nothing pending: a cause we do not decode. */
        dumpregs(tr, grp);
        panic("trap");
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
