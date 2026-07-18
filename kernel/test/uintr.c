/*
 * uintr -- exercise the corrected external-interrupt gate (kernel/besm6.S:intrgate) from USER
 * mode, on the real machine.
 *
 * The gate must preserve the whole visible machine across a C interrupt handler and switch to
 * the kernel stack when the interrupt came from user code.  sctest enters the gate from KERNEL
 * mode, so it cannot see the stack switch (there is nothing to switch) and passes even if that
 * leg is missing.  This test forges a genuine user-mode context and takes an external interrupt
 * in it, so all four fixes -- R, Y (РМР), M[16] (M[020]) and the r15 switch -- are under test.
 *
 * How it works (see kernel/test/crt0u.s for the asm half and doc/Context_Switch.md §14):
 *   1. sureg() builds a user map: uprog's own physical page at virtual page 0 (so the forged
 *      user runs mapped, executing the real code), two data pages at virtual 2-3, one stack.
 *   2. A sentinel is placed at virtual MODVAL, the address the forged M[16] modifier points at.
 *   3. Physical USPV (= the forged user r15) is seeded with KSENT: if intrgate fails to switch the
 *      stack, extintr()'s frame lands there (r15 is a physical index once the trap forces БлП on)
 *      and overwrites it.
 *   4. The console "printing finished" interrupt is armed and kicked; we poll until it is
 *      pending (interrupts are still masked by БлПр in supervisor), then gouser() forges the
 *      four registers and `выпр's into user mode.  The pending interrupt fires at uprog's first
 *      instruction.
 *   5. intrgate saves/switches/handles/restores; uprog reads the four values back and traps out
 *      via `стоп' -> extracode э63 -> report(), which compares and halts.
 *
 * main() never returns; report() (crt0u.s) leaves the fault mask in the accumulator, and
 * uintr.ini asserts ACC == 0.  A nonzero ACC names the failing check (1 M[16], 2 R, 4 Y,
 * 010 r15 value, 020 stack-switch).
 */

// clang-format off
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/dir.h"
#include "sys/user.h"
#include "sys/proc.h"
#include "sys/text.h"
#include "sys/seg.h"
#include "sys/besm6dev.h"
// clang-format on

#include <besm6.h>

/*
 * The kernel globals utab.o refers to.  In the kernel `u' is absolute at 076000 and maxmem is
 * counted at boot; here they are ordinary storage (as in mmutest).
 */
struct user u;
int maxmem = 512 * 1024;

static struct proc pr;
static struct text tx;

/* crt0u.s */
extern unsigned uprogadr;      /* uprog's link-time word address, as a plain integer */
void gouser(unsigned uentry);

/* brz.s */
void drainbrz(void);

/* Must match the EQUs in crt0u.s. */
#define MODVAL 04010U          /* virtual data address the forged modifier points at */
#define SENT   0525252U        /* the sentinel poked there */
#define USPV   074000U         /* forged user r15 == physical stack-switch sentinel page */
#define KSENT  0333333U        /* seed value at physical USPV / USPV+1 */

#define IMAGEPG 16             /* physical page of the process image (data + stack), free memory */

/*
 * увв 031 simulates a ГРП interrupt: GRP |= (ACC & 0xFFFFFF) << 24.  It is the deterministic,
 * self-contained interrupt source this test uses instead of a device -- no console, no timing.
 * GRP_TIMER (bit 40) is a plain flip-flop (not one of GRP_WIRED_BITS), so extintr() dismisses it
 * with an ordinary MOD_GRPCLR.
 */
#define EXT_GRPSET 031U

/*
 * The external interrupt handler, reached from crt0u.s's intrgate.  It only has to dismiss the
 * simulated interrupt so the machine leaves the handler -- but being ordinary C, it clobbers R
 * (the ABI exits NTR 3 / ω = logical), Y (the logical ops) and M[16] (the `nintr++' global
 * reach), which is exactly the state intrgate must have saved.  If intrgate drops any of those
 * saves, uprog sees the damage and report() flags it.
 */
unsigned nintr; /* bumped through the compiler's `utc nintr' idiom -> clobbers M[16] */

void extintr(void)
{
    nintr++;
    if (__besm6_mod(MOD_GRP, 0) & GRP_TIMER) {
        __besm6_mod(MOD_GRPCLR, ~GRP_TIMER); /* dismiss it ... */
        __besm6_mod(MOD_MGRP, 0);            /* ... and mask, so the interval timer cannot re-fire */
    }
}

int main()
{
    unsigned uaddr, uentry;

    /*
     * Build the user map.  uprog runs mapped at virtual page 0, so virtual page 0 must map to
     * uprog's OWN physical page (the real code), not a scratch page.  Text is two pages, data
     * two, stack one -- the same shape mmutest uses.
     */
    uaddr  = uprogadr;                 /* uprog's WORD address (a plain integer from the linker) */
    uentry = uaddr & (PGSZ - 1);       /* its offset within virtual page 0 */

    tx.x_caddr = uaddr & ~(PGSZ - 1);  /* map virtual page 0 -> uprog's physical page */
    tx.x_size  = 2 * PGSZ;

    pr.p_addr  = IMAGEPG * PGSZ;
    pr.p_size  = USIZE + 3 * PGSZ;
    pr.p_textp = &tx;

    u.u_procp = &pr;
    u.u_tsize = 2 * PGSZ;
    u.u_dsize = 2 * PGSZ;
    u.u_ssize = PGSZ;

    sureg();

    /*
     * The sentinel the modifier-armed first user instruction will read.  Write it PHYSICALLY
     * (physaddr() of the virtual address) rather than through poke() -- poke()'s bare `vtm 2/3'
     * would clear БлПр and enable interrupts in supervisor.  drainbrz() then settles the
     * physical-tagged store so the user's later mapped read sees it under the cache.
     */
    *(volatile unsigned *)physaddr(MODVAL) = SENT;
    drainbrz();

    /*
     * The stack-switch sentinel: seed physical USPV/USPV+1.  intrgate on the user r15 (switch
     * missing) would overwrite it; intrgate on the kernel stack (switch present) leaves it.
     */
    *(volatile unsigned *)USPV       = KSENT;
    *(volatile unsigned *)(USPV + 1) = KSENT;
    drainbrz();

    /*
     * Arm and raise a single external interrupt, synchronously: unmask GRP_TIMER in МГРП, then
     * simulate it with увв 031.  БлПр is still set (interrupts masked) all through main, so the
     * bit sits PENDING; the instant gouser's `выпр' clears БлПр it fires -- at uprog's very first
     * instruction, which is what makes the modifier-armed M[16] test deterministic.
     */
    __besm6_mod(MOD_MGRP, GRP_TIMER);
    __besm6_ext(EXT_GRPSET, (unsigned)(GRP_TIMER >> 24));

    gouser(uentry); /* forge the user context and enter it; never returns */
    return 0;
}
