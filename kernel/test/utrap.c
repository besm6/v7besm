/*
 * utrap -- exercise the fault gate (kernel/besm6.S:trapgate) and the restart protocol from
 * USER mode, on the real machine.
 *
 * uintr covers the other asynchronous door (0501, an external interrupt).  This one covers
 * 0500: a data-protection fault, taken in forged user mode, must
 *
 *   1. save the whole visible machine into the reg.h frame on the KERNEL stack (not on the
 *      user's r15, which is an unmapped physical index once the trap forces БлП on),
 *   2. hand trap() a pointer that ALIASES that frame -- so what trap() writes is what the
 *      epilogue reloads,
 *   3. let trap() back the PC up over the faulting instruction (the restart protocol:
 *      the machine vectors with the PC already advanced past it), and
 *   4. resume the user so the faulting instruction RE-EXECUTES.
 *
 * How it works (crt0t.S is the asm half):
 *   - The map leaves virtual pages 4, 5 and 6 closed.  uprog reads page 4, then page 5, then
 *     page 6.  The first two faults are taken from opposite halves of their words, so both
 *     arms of the PC fixup are exercised; each faulting `xta' stores what it read, so an
 *     instruction that was skipped instead of retried leaves the wrong word behind.
 *   - trap() below checks the frame against the values gouser() forged, backs the PC up,
 *     opens the page that faulted and returns.  It clobbers R, Y and M[16] on the way (any C
 *     does), so the NEXT fault's frame shows whether the epilogue restored them.
 *   - The third fault means "done": trap() folds everything into a mask and calls halt(),
 *     which stops the machine with the mask in the accumulator.
 *
 * utrap.ini asserts ACC == 0.  A nonzero ACC names the failing check -- see the F_* bits.
 */

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

/*
 * The kernel globals utab.o refers to.  In the kernel `u' is absolute at 076000 and maxmem is
 * counted at boot; here they are ordinary storage (as in mmutest and uintr).
 */
struct user u;
int maxmem = 512 * 1024;

static struct proc pr;
static struct text tx;

/* crt0t.S */
extern unsigned uprogadr; /* uprog's link-time word address, as a plain integer */
extern int *ustkbase;     /* base of the gate's stack: where the trap frame lands */
void gouser(unsigned uentry);
void halt(unsigned mask);

/* brz.s */
void drainbrz(void);

/* psw.s -- reads PSW back, the only way to see the interrupt level from C */
int getpsw(void);

/* Must match the EQUs in crt0t.S. */
#define RMODE  044U     /* forged R */
#define RMRVAL 0123456U /* forged Y (РМР) */
#define MODVAL 04010U   /* forged M[16] */
#define USPV   074000U  /* forged r15, and the physical stack-switch sentinel page */
#define KSENT  0333333U /* seed value at physical USPV / USPV+1 */
#define VSLOT1 04000U   /* virtual page 2: where uprog reports the fault-1 retry */
#define VSLOT2 04001U   /*                 ... and the fault-2 retry */

#define IMAGEPG 16 /* physical page of the process image (data + stack), free memory */

/*
 * `rte 07777' reads R left-aligned to bit 48, so the forged R = 044 arrives in the frame as
 * bits 47 and 44 -- the same constant crt0u.S's report() compares against, spelled in C.
 * Read the frame through an `unsigned *' rather than through struct trap's `int' fields: an
 * int is 41 bits on this machine (doc/Besm6_Data_Representation.md §8) and would not hold it.
 */
#define RFRAMED (((unsigned)1 << 46) | ((unsigned)1 << 43))

/* The sentinels behind the two closed pages that get opened. */
#define SENT1 0525252U
#define SENT2 0313131U

/* Physical base of the data region == the physical address of virtual page 2. */
#define DBASE (IMAGEPG * PGSZ + USIZE)

/* Fault-mask bits, reported in the accumulator by halt().  Zero means every check passed. */
#define F_CREG   0001 /* M[16] not preserved across the gate */
#define F_RREG   0002 /* R not preserved */
#define F_RMR    0004 /* Y (РМР) not preserved */
#define F_R15    0010 /* the framed r15 is not the user's */
#define F_STACK  0020 /* the stack was not switched: trap()'s frame landed on the user r15 */
#define F_USER   0040 /* SPSW does not say the fault came from user mode */
#define F_CAUSE  0100 /* wrong ГРП cause or wrong faulting page */
#define F_SLOT1  0200 /* fault 1 was skipped, not retried (restart protocol) */
#define F_SLOT2  0400 /* fault 2 was skipped, not retried */
#define F_NFAULT 01000 /* the wrong number of faults arrived */
#define F_IPL    02000 /* the gate dispatched with БлПр still set: the level was never opened */
#define F_KTRAP  04000 /* the gate took the SUPERVISOR arm: it read the forged SPSW wrongly */

static unsigned mask;   /* accumulated failures */
static unsigned nfault; /* which fault we are in: 1, 2, 3 */

/*
 * The fault handler, reached from crt0t.S's trapgate -- the same C signature the real
 * kernel's trap() has, since the gate is the same gate: nothing is passed, and the frame
 * is found at the base of the stack the gate switched to.
 *
 * This is where the test must diverge in SPELLING, though not in logic.  kernel/trap.c says
 * `u.u_stack', because there the u-area is the fixed page at 076000 and its tail IS the
 * kernel stack; here `u' above is ordinary storage that utab.o wants and the gate's stack is
 * crt0t.S's `kstack', named by the same `ustkbase' cell the gate loads M15 from.
 */
void trap(void)
{
    struct trap *tr = (struct trap *)ustkbase;
    unsigned *f     = (unsigned *)tr; /* the frame, aliased in place */
    unsigned grp, page, want;

    nfault++;

    /*
     * The restart protocol -- a copy of kernel/trap.c's.  KEEP THE TWO IN STEP: this test
     * cannot link the kernel's trap.c (which pulls in the whole kernel), so the six lines
     * are duplicated here, exactly as the gate itself is duplicated in crt0t.S.
     */
    if (tr->spsw & SPSW_NEXT_RK) {
        tr->ret--; /* the saved PC is the faulting WORD plus one */
        tr->spsw &= ~SPSW_NEXT_RK;
    }

    /*
     * The gate opened the interrupt level before dispatching.  БлПр is forced on at the vector,
     * and the gate clears it again -- but only for a fault FROM USER, which is what this test
     * takes.  Without the check the discriminator in crt0t.S could branch either way and utrap
     * would still pass.  There is no C-visible shadow of the level; it has to be read out of PSW.
     */
    if (getpsw() & PSW_INTR_DISABLE)
        mask |= F_IPL;

    /* Did we come from user mode, on the kernel stack, with the machine intact? */
    if (!USERMODE(tr->spsw))
        mask |= F_USER;
    if (f[CREG] != MODVAL)
        mask |= F_CREG;
    if (f[RREG] != RFRAMED)
        mask |= F_RREG;
    if (f[RMR] != RMRVAL)
        mask |= F_RMR;
    if (f[R15] != USPV)
        mask |= F_R15;
    if (*(volatile unsigned *)USPV != KSENT || *(volatile unsigned *)(USPV + 1) != KSENT)
        mask |= F_STACK;

    /* The cause, read live from ГРП, and the faulting page (bits 5-9, data faults only). */
    grp  = __besm6_mod(MOD_GRP, 0);
    page = (grp & GRP_PAGE_MASK) >> GRP_PAGE_SHIFT;
    want = 3 + nfault; /* pages 4, 5, 6 in order */
    if (!(grp & GRP_OPRND_PROT) || page != want)
        mask |= F_CAUSE;
    __besm6_mod(MOD_GRPCLR, ~GRP_OPRND_PROT);

    if (nfault >= 3) {
        /*
         * Done.  The two retries have reported what their re-executed `xta' read; drain the
         * write cache first, because those stores were MAPPED and this reads them physically.
         */
        drainbrz();
        if (*(volatile unsigned *)DBASE != SENT1)
            mask |= F_SLOT1;
        if (*(volatile unsigned *)(DBASE + 1) != SENT2)
            mask |= F_SLOT2;
        if (nfault != 3)
            mask |= F_NFAULT;
        halt(mask); /* never returns */
    }

    /*
     * Open the page that faulted -- the test's stand-in for grow() -- and return.  The gate's
     * epilogue resumes uprog at the faulting instruction, which now reads the sentinel.
     */
    u.u_dsize += PGSZ;
    sureg();
}

/*
 * The OTHER door behind 0500 (kernel/trap.c: ktrap()).  crt0t.S branches here when SPSW says the
 * fault came from supervisor -- which this test never arranges: gouser() forges a user-mode SPSW
 * and every fault below comes from uprog.  Reaching it means the gate's discriminator read the
 * mode wrongly, so say so and stop; there is nothing to return to, ktrap() being a branch target
 * and not a call.
 */
void ktrap(void)
{
    halt(mask | F_KTRAP); /* never returns */
}

int main()
{
    unsigned uaddr, uentry;

    /*
     * Build the user map: uprog's OWN physical page at virtual page 0 (so the forged user runs
     * the real code), text two pages, data two -- which leaves virtual pages 4, 5 and 6 closed,
     * and those are the ones uprog walks into.
     */
    uaddr  = uprogadr;           /* uprog's WORD address (a plain integer from the linker) */
    uentry = uaddr & (PGSZ - 1); /* its offset within virtual page 0 */

    tx.x_caddr = uaddr & ~(PGSZ - 1); /* map virtual page 0 -> uprog's physical page */
    tx.x_size  = 2 * PGSZ;

    pr.p_addr  = IMAGEPG * PGSZ;
    pr.p_size  = USIZE + 4 * PGSZ + PGSZ;
    pr.p_textp = &tx;

    u.u_procp = &pr;
    u.u_tsize = 2 * PGSZ;
    u.u_dsize = 2 * PGSZ;
    u.u_ssize = PGSZ;

    sureg();

    /*
     * Seed the pages that are still closed, physically (the kernel runs unmapped, so a kernel
     * address IS a physical address), and settle the write cache so the user's later mapped
     * reads see them.  These are the words the retried instructions must come back with.
     */
    *(volatile unsigned *)(DBASE + 2 * PGSZ) = SENT1;
    *(volatile unsigned *)(DBASE + 3 * PGSZ) = SENT2;

    /*
     * The stack-switch sentinel: seed physical USPV/USPV+1.  If the gate failed to switch r15,
     * trap()'s C frame would run on the user's r15 -- a physical index, БлП being forced on --
     * and overwrite this.
     */
    *(volatile unsigned *)USPV       = KSENT;
    *(volatile unsigned *)(USPV + 1) = KSENT;
    drainbrz();

    /*
     * Mask every interrupt source: the forged SPSW enters user mode with БлПр CLEAR, and the
     * interval timer re-arms GRP_TIMER at reset.  Nothing here wants an external interrupt.
     */
    __besm6_mod(MOD_MGRP, 0);

    gouser(uentry); /* forge the user context and enter it; never returns */
    return 0;
}
