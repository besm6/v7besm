/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 1999 Robert Nordier. All rights reserved. */

/*
 * The extracode door: the C side of the 0577 syscall gate and of the 0550-0576
 * catch-all (kernel/besm6.S: `sysgate' and `badext').
 *
 * This is a SEPARATE FILE, not part of trap.c, for the same reason brz.s, uarea.s,
 * seg.s and usermem.s are separate: kernel/test/usys links the real dispatcher
 * against a hand-built process, and it cannot link trap.c -- which drags in printf,
 * the signal machinery and grow(), and with them the rest of the kernel.  Keeping
 * the marshalling here is what lets the test exercise the real thing rather than a
 * copy of it.  (The plan had put this in trap.c; linking usys is what moved it.)
 *
 * An extracode never reaches trap(): the hardware does not funnel it through 0500.
 * It vectors э50-э77 straight to 0550-0577, one word each, and hands the handler
 * the effective address in r14.
 */

// clang-format off
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/dir.h"
#include "sys/user.h"
#include "sys/proc.h"
#include "sys/reg.h"
#include "sys/seg.h"
// clang-format on

/*
 * Dispatched when the user names a syscall number outside sysent[].  The number
 * is the effective address of a `$77 N', which the user can index-modify to any
 * 15-bit value, so this is a reachable path and not an assertion.
 */
static struct sysent badsysent = { 0, 0, nosys };

/*
 * The tail both doors share with trap(): deliver any signal the call raised,
 * re-price the process, and give up the CPU if something more urgent is runnable.
 */
static void sysret(struct trap *tr, time_t syst)
{
    if (issig())
        psig();
    curpri = setpri(u.u_procp);
    if (runrun)
        qswtch();
    if (u.u_prof.pr_scale)
        addupc(tr->ret, &u.u_prof, (int)(u.u_stime - syst));
}

/*
 * Extracode э77 -- the Unix system call.  Called from `sysgate' (besm6.S).
 *
 * The gate passes nothing.  Its stack switch is unconditional -- э77 only ever
 * comes from user mode -- so the reg.h frame it built is always at the base of the
 * kernel stack, the link-time constant u.u_stack.  `tr' is that frame IN PLACE, so
 * a register written here is a register the gate's epilogue reloads on the way out.
 *
 * No PC fixup, unlike trap(): the extracode gate stores nextpc in ERET, so
 * SPSW_NEXT_RK is never set by this door and there is nothing to back up.
 */
void syscall(void)
{
    register struct trap *tr = (struct trap *)u.u_stack;
    register struct sysent *callp;
    register int i, n;
    unsigned code;
    int *ap;
    time_t syst;

    syst      = u.u_stime;
    u.u_ar0   = (int *)tr;
    u.u_error = 0;

    /*
     * The syscall number is the effective address the hardware wrote to r14 at the
     * vector.  Latch it NOW: R_ERRNO is that same slot, which the return path below
     * overwrites.  And RANGE-CHECK it rather than masking -- the user can
     * index-modify the effective address to any 15-bit value, and a mask would fold
     * an out-of-range number onto a real syscall instead of dispatching nosys.
     *
     * r14's OTHER ABI role -- the negative argument count -- is irrelevant here and
     * is deliberately not read: the count comes from sysent[], which is
     * authoritative.  b6sim ignores r14 on input for the same reason
     * (doc/Aout_Simulator.md sec 3).
     */
    code  = (unsigned)tr->r14;
    callp = (code < NSYSENT) ? &sysent[code] : &badsysent;

    /*
     * Marshal the arguments, INVERTING the caller's layout.  The BESM-6 convention
     * passes argument k of n at r15 - (n-k) with the LAST one in the accumulator --
     * descending in memory, and one of them not in memory at all.  The ~35 callees
     * read (struct a *)u.u_ap as an ASCENDING array in prototype order, so the two
     * views have to be reconciled here.
     *
     * The arguments live in USER space, so this is fuword(), not a kernel
     * dereference.  The framed r15 is right there in the frame, so nothing has to be
     * read before the stack switch.  `ap' is built as an int * and converted by the
     * compiler: a char * is a fat pointer and one assembled by hand from an int has
     * the wrong marker bit (kernel/usermem.s).
     */
    n = callp->sy_narg;
    if (n > 0) {
        ap = (int *)tr->r15 - (n - 1);
        for (i = 0; i < n - 1; i++)
            u.u_arg[i] = fuword((caddr_t)ap++);
        u.u_arg[n - 1] = tr->acc;
    }

    /*
     * The gate stands in for a called function, so it owes the caller the callee's
     * stack cleanup: pop the n-1 words pushed below r15 (the nth travelled in the
     * accumulator and was never pushed).  cmd/sim/syscall.cpp does the same; without
     * it the user stack drifts by a word per multi-argument syscall.
     *
     * Do it BEFORE the dispatch, not after.  exec() reseeds r15 on success, and
     * sendsig() builds its signal frame on the user stack -- both want the popped
     * value, and a pop afterwards would corrupt the first and come too late for the
     * second.  The arguments are already copied out, so nothing below reads the user
     * stack at the old depth.
     */
    if (n >= 2)
        tr->r15 -= n - 1;

    u.u_dirp     = (caddr_t)u.u_arg[0];
    u.u_r.r_val1 = 0;
    u.u_r.r_val2 = 0;
    u.u_ap       = u.u_arg;
    if (save(u.u_qsav)) {
        /* the EINTR path: a signal longjmp'd back here out of sleep() */
        if (u.u_error == 0)
            u.u_error = EINTR;
    } else {
        (*callp->sy_call)();
    }

    /*
     * Return through the frame.  There is no carry flag on this machine: a failed
     * call is -1 in the accumulator with errno in r14, exactly as b6sim reports it,
     * and a successful one leaves r14 zero.  r13 is untouched either way -- it is
     * the caller's return address (see R_VAL2 in reg.h).
     */
    if (u.u_error) {
        tr->acc = -1;
        tr->r14 = u.u_error;
    } else {
        tr->acc = u.u_r.r_val1;
        tr->r12 = u.u_r.r_val2;
        tr->r14 = 0;
    }

    sysret(tr, syst);
}

/*
 * Extracodes э50-э76 -- ones this kernel does not implement.  Called from `badext'
 * (besm6.S), which is `sysgate' with a different C target; the frame is the same and
 * is in the same place.
 *
 * Two things arrive here that do not look like extracodes: э20/э60 and э21/э61 alias
 * to one vector word each (the hardware maps э20/э21 to 0540 + (opcode >> 3)), and a
 * user `стоп' is re-dispatched as э63.
 */
void badextr(void)
{
    register struct trap *tr = (struct trap *)u.u_stack;
    time_t syst;

    syst    = u.u_stime;
    u.u_ar0 = (int *)tr;

    psignal(u.u_procp, SIGINS);
    sysret(tr, syst);
}
