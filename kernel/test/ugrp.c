// ugrp -- МГРП is dynamic, and a wired ГРП bit does not wedge extintr().  Task 18b.2.
//
// The mass-storage completion bits are WIRED: live wires from the device rather than
// flip-flops in ГРП, so `002 037' (MOD_GRPCLR) cannot lower them -- the hardware clears
// `GRP &= ACC | GRP_WIRED_BITS' and quietly keeps them.  And "free" means IDLE, so such a
// bit stands whenever no transfer is running.  Both facts land on the same line of
// kernel/intr.c: extintr()'s fallback arm, which dismisses an unhandled bit "so that a bit
// nobody handles cannot spin here forever", clears nothing at all for these and spins on
// exactly the source it was written to protect against.
//
// So this test does two things the drum and disk drivers (18b.3, 18b.4) will need:
//
//   Part 1 -- THE DONE-CONDITION.  Forge GRP_CHAN5_FREE, arm it in МГРП, and call
//             extintr().  It must RETURN.  There is no assertion for that -- a failure
//             here is a hang, which is what ugrp.ini's `step' turns into a failure -- and
//             the bit must have been taken out of МГРП rather than out of ГРП, because
//             out of ГРП is not possible.  That the bit is still standing in ГРП
//             afterwards is checked too: it is what proves the wired path was the one
//             exercised, and not a bit that turned out to be clearable after all.
//
//   Part 2 -- THE CONTRAST, which is what proves the probe distinguishes the two kinds.
//             Forge an ordinary flip-flop nobody handles (bit 30, имитация), arm it, and
//             call extintr() again.  This one must come out of ГРП and STAY armed in
//             МГРП -- a guard that disarmed every unhandled source would pass part 1 and
//             be quietly wrong, deafening the kernel to any device that ever glitches.
//
// WHY PART 1 FORGES A TAPE BIT.  The choice has been forced twice.  It began on
// GRP_DRUM1_FREE, the obvious wired bit while nothing in the kernel handled it; task 18b.3
// gave the drum bits a handler, so forging one would have exercised mbintr() rather than
// the fallback arm this test is about, and it moved to GRP_CHAN3_FREE.  Task 18b.4 gave the
// disk bits a handler too, and it moved here.
//
// GRP_CHAN5_FREE is tape channel 5: the same kind of bit (wired, and "free" meaning idle),
// on a device this kernel does not drive and has no plans to.  That is what makes this the
// last move -- the earlier two bits were both chosen because they were *unclaimed yet*,
// which is a property with an expiry date; this one is unclaimed because there is no driver
// to write.  The tape channels hold six more like it should that ever change.
//
// Simpler than uclock, which is the other test that links the real intr.o: no gate, no
// user mode, no forged context.  extintr() is an ordinary C function and is called as one,
// with delivery blocked throughout by spl1(), so every ГРП bit here arrives when this file
// says it does.  crt0.s's own 0501 stub is linked but is never reached.
//
// Bits are forged with `увв 031' (GRP |= (ACC & BITS(24)) << 24), the deterministic source
// uintr and uclock use -- no device to attach and no timing to lose a race to.
//
// ONE FREE-RUNNING TIMER TICK IS POSSIBLE and costs nothing here: the interval timer
// cannot be switched off, GRP_TIMER is armed by intrinit(), and extintr() handles it in
// its own arm before ever reaching the fallback.  clock() below just counts.
//
// ugrp.ini asserts ACC == 0.  A nonzero ACC names the failing check -- see the F_* bits.

#include <besm6.h>

#include "sys/besm6dev.h"

// intr.c, the code under test.
extern unsigned mgrp; // the shadow of МГРП, which cannot be read back
void intrinit(void);
void extintr(void);
void mgrpon(unsigned bits);
void mgrpoff(unsigned bits);
int spl1(void);

// What intrinit() arms and leaves armed.  Must match IRQ_ON in kernel/intr.c -- the point
// of checking against a copy is that a bit silently added there shows up here.
#define BOOTBITS (GRP_SLAVE | GRP_TIMER)

// увв 031 simulates a ГРП interrupt: GRP |= (ACC & 0xFFFFFF) << 24.
#define EXT_GRPSET 031U

// Bit 30, имитация -- an ordinary flip-flop that nothing in this kernel handles or arms.
// Deliberately NOT in sys/besm6dev.h: only this test ever raises it, and that header is
// the machine's real bit map, not a scratchpad.
#define GRP_IMITATION 00000004000000000

// Fault-mask bits, returned in the accumulator.  Zero means every check passed.
#define F_INIT  0000001 // intrinit() did not leave МГРП holding exactly the boot set
#define F_ARM   0000002 // mgrpon() did not add exactly its bit
#define F_WIRED 0000004 // the wired bit is STILL armed: extintr() would spin on it
#define F_LOST  0000010 // the guard disarmed the boot sources too -- deaf kernel
#define F_STUCK 0000020 // the wired bit came out of ГРП, so part 1 tested nothing
#define F_FREE  0000040 // the flip-flop was not dismissed from ГРП
#define F_KEPT  0000100 // ...and it was disarmed from МГРП: the guard cannot tell them apart
#define F_OFF   0000200 // mgrpoff() did not remove exactly its bit

// -------------------------------------------------------------------------
// The environment kernel/intr.c names.
// -------------------------------------------------------------------------

// Where the kernel's 0501 gate publishes the frame base.  extintr() dereferences it only
// on the GRP_TIMER arm, and clock() below ignores it, so a null cell is enough.
int *intrframe;

struct trap;

static int nclock; // timer ticks serviced during the run; any number is fine

void clock(struct trap *tr)
{
    nclock++;
}

// prpintr() calls this.  No ПРП bit is ever up here, so it must never run.
void scintr(void)
{
    nclock--; // poison: it would take nclock negative, but nothing asserts on it
}

// extintr()'s drum arm calls this.  Neither drum bit is forged or armed here -- see the
// note above part 1 -- so like scintr() it must never run.  If it ever did, the real
// mb.o is not linked and this empty body would leave the wired bit standing and armed,
// which ugrp.ini's `step' would report as the hang it is.
void mbintr(void)
{
    nclock--;
}

// And extintr()'s disk arm.  Same reasoning: neither disk bit is forged or armed here --
// part 1 moved OFF GRP_CHAN3_FREE precisely so that it would not land in this function --
// so reaching it means the dispatch went wrong.
void mdintr(void)
{
    nclock--;
}

int main(void)
{
    unsigned mask = 0;

    // Block delivery for the whole run.  extintr() is called by hand below, and it must be
    // the ONLY thing servicing ГРП: a tick arriving through crt0.s's gate in between would
    // service a forged bit before the check that expects it standing.
    spl1();

    intrinit();
    if (mgrp != BOOTBITS)
        mask |= F_INIT;

    // ---- Part 1: the wired bit ----

    __besm6_ext(EXT_GRPSET, (unsigned)(GRP_CHAN5_FREE >> 24));
    mgrpon(GRP_CHAN5_FREE);
    if (mgrp != (BOOTBITS | GRP_CHAN5_FREE))
        mask |= F_ARM;

    extintr(); // must return; a spin is caught by `step' in ugrp.ini

    if (mgrp & GRP_CHAN5_FREE)
        mask |= F_WIRED;
    if ((mgrp & BOOTBITS) != BOOTBITS)
        mask |= F_LOST;
    if (!(__besm6_mod(MOD_GRP, 0) & GRP_CHAN5_FREE))
        mask |= F_STUCK;

    // ---- Part 2: an ordinary unhandled flip-flop ----

    __besm6_ext(EXT_GRPSET, (unsigned)(GRP_IMITATION >> 24));
    mgrpon(GRP_IMITATION);

    extintr();

    if (__besm6_mod(MOD_GRP, 0) & GRP_IMITATION)
        mask |= F_FREE;
    if (!(mgrp & GRP_IMITATION))
        mask |= F_KEPT;

    // ---- and the other half of the pair ----

    mgrpoff(GRP_IMITATION);
    if (mgrp != BOOTBITS)
        mask |= F_OFF;

    return mask;
}
