/*
 * BESM-6 interrupt dispatch.
 *
 * The machine has one external interrupt vector (address 0501) and one interrupt
 * register, ГРП, whose bits say what happened.  besm6.S saves the interrupted
 * registers and calls extintr() below; everything else is C.
 *
 * ПРП, the peripheral interrupt register, has no interrupt line of its own.  The
 * processor raises GRP_SLAVE (ГРП bit 37) for as long as any unmasked ПРП bit is
 * up, and re-tests it before every instruction.  So a peripheral interrupt arrives
 * as a ГРП interrupt, and prpintr() has to read ПРП to find out which device it
 * was.  It also means the ordering below is load-bearing: the device's ПРП bit is
 * cleared first and GRP_SLAVE dismissed afterwards.  The other way round re-raises
 * GRP_SLAVE at once and the machine never leaves the handler.
 *
 * Both masks are write-only -- there is no address that reads МГРП or МПРП back --
 * so the kernel keeps shadows of them.  A driver must never write a mask itself: it
 * would silently drop every other device's bits.  Use mprpon().
 *
 * See doc/Besm6_Peripherals.md for the registers and doc/Intrinsics.md for the
 * intrinsics; kernel/test/sctest.c exercises this path against SIMH.
 */

// clang-format off
#include "sys/types.h"
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/besm6dev.h"
// clang-format on

#include <besm6.h>

void scintr(void);
void mbintr(void); /* the drum driver's half of the dispatch below (kernel/dev/mb.c) */
void mdintr(void); /* and the disk driver's (kernel/dev/md.c) */

/*
 * The frame the 0501 gate built, and the handler that wants it.  besm6.S publishes the
 * frame base in `intrframe' because that gate's stack switch is conditional and the base
 * is therefore a run-time value -- see the header over intrgate.  `struct trap' is only
 * forward-declared: this file hands the pointer on and has no business knowing its shape.
 */
extern int *intrframe;
struct trap;
void clock(struct trap *tr);

/*
 * Interrupt priority level.
 *
 * The BESM-6 has no priority hierarchy, so rather than pretend to the eight graded levels
 * of the PDP-11, this kernel has exactly two -- interrupts enabled and
 * interrupts blocked.  Only spl0 enables; every splN above it blocks.  So only spl0() and
 * spl1() exist here; spl4()...spl7() are macros for spl1() in sys/systm.h.  Callers keep
 * the v7 spelling (`s = spl5(); ...; splx(s);`) and still get what they were really after:
 * on a uniprocessor with no atomic instruction, masking interrupts is the only lock there is.
 *
 * TWO REGISTERS, TWO JOBS.  Delivery needs БлПр clear AND `ГРП & МГРП' non-zero, so either
 * could serve as the mask.  They are not interchangeable, and this kernel divides them:
 *
 *   БлПр (PSW bit 02000) is the PRIORITY.  spl1() sets it to block and spl0() clears it to
 *   enable.  It is the right choice because the hardware already treats it as one: an
 *   interrupt or extracode forces БлПр on at the vector and
 *   `выпр' restores it from SPSW, so a gate return re-establishes the level by itself --
 *   exactly what the PDP-11's `rtt' does when it reloads the priority field of PS.  МГРП,
 *   being a separate write-only register outside the mode word, does nothing of the kind.
 *
 *   Setting it costs ONE INSTRUCTION either way.  __besm6_maskpsw() is the register-0 `уиа',
 *   the mode write, which takes БлП, БлЗ and БлПр straight from its own address field -- see
 *   the PSW_KERNEL comment in sys/besm6dev.h for the invariant the other two re-assert, and
 *   doc/Intrinsics.md §3.3.  The mask rides in that address field, so it must be a
 *   compile-time constant: the two levels are two different instructions, not one instruction
 *   over a variable.  spl0() and spl1() each name their own outright.  splx() cannot -- its
 *   level is a run-time value -- so it takes the OTHER mode write, the general `ати' of
 *   __besm6_setpsw(), and puts back the whole word its cookie carries.
 *
 *   WHICH IS WHY THE COOKIE IS A PSW WORD, not a small integer: spl1() hands back what `ита'
 *   read, and splx() writes exactly that back.  Nothing may compare a cookie against a level,
 *   synthesize one, or splx() a constant -- splx(0) would clear БлП and БлЗ and drop the kernel
 *   into its own user's address space.  There is no software shadow of the level to compare
 *   with either: PSW is the one machine register that reads back, so the hardware bit is the
 *   only copy, and it is `выпр' on the gate return that restores it.
 *
 *   The `ати' is a WIDER WRITE than the `уиа': it also restores ПоП, ПоК and the write-watch
 *   bit from the cookie, which the masked write leaves alone (see sys/besm6dev.h).  Nothing in
 *   this kernel touches those three, so a cookie always carries them back unchanged -- but code
 *   that starts to must not straddle an spl bracket.
 *
 *   МГРП is the SOURCE ENABLE.  extintr()'s `grp & mgrp' means "sources this kernel is
 *   listening to right now", which is what that mask was always for.  It is NOT constant:
 *   intrinit() arms the sources that are always live (IRQ_ON), and a driver arms its own
 *   completion bit for the duration of one exchange with mgrpon()/mgrpoff() below.  The
 *   comment at GRP_DRUM1_FREE in sys/besm6dev.h is why -- the mass-storage "free" bits are
 *   WIRED and mean IDLE, so one left standing in МГРП wedges extintr() the moment the
 *   device goes quiet.
 *
 * No spl touches МГРП, and none must start to.  The level is БлПр alone; making it a per-level
 * МГРП table again would cost a 002 036 write on every spl() and buy nothing over two levels
 * -- and it is the arrangement that failed, below.
 *
 * It used to be the other way round -- the spls rewrote МГРП and nothing ever touched БлПр
 * -- and the two mechanisms fought: the gates hold БлПр from the vector, so spl0() opened
 * МГРП while БлПр still blocked everything, and no interrupt could be taken in kernel mode
 * at all.  That was invisible while the only interrupts arrived in user mode, and became
 * load-bearing the moment idle() needed to spin waiting for one.
 *
 * IRQ_ON is the set of sources armed at BOOT and left armed -- the ones that are always
 * live, with no exchange to bracket them.  It is not "every source this kernel can
 * service": a device whose interrupt means something only while a transfer is running
 * arms itself through mgrpon() instead, and none of the mass-storage completion bits may
 * ever appear here (see the comment over them in sys/besm6dev.h).
 *
 * Unsigned because ГРП is 48 bits wide and a signed int on this target holds only 41.
 */
#define IRQ_ON (GRP_SLAVE | GRP_TIMER)

unsigned mgrp; /* shadow of МГРП: it cannot be read back */
unsigned mprp; /* shadow of МПРП: likewise */

/*
 * Arm the always-live interrupt sources.  Called once from main(), before the first
 * spl0().  After this the priority rides on БлПр alone; МГРП changes only through
 * mgrpon()/mgrpoff(), and only for the length of one exchange.
 */
void intrinit(void)
{
    mgrp = IRQ_ON;
    __besm6_mod(MOD_MGRP, mgrp);
}

void splx(int s)
{
    __besm6_setpsw(s); /* put back the mode word spl1() handed out, БлПр with it */
}

void spl0(void)
{
    __besm6_maskpsw(PSW_KERNEL); /* clear БлПр -- delivery enabled */
}

int spl1(void)
{
    int old = __besm6_getpsw();

    __besm6_maskpsw(PSW_KERNEL | PSW_INTR_DISABLE); /* set БлПр -- interrupts blocked */
    return old;
}

/*
 * The idle loop, called from swtch() when nothing is runnable (and from panic()).
 *
 * THERE IS NO WAIT-FOR-INTERRUPT ON THIS MACHINE.  The only halt is 033 (стоп), which is
 * resumable on real hardware only from the operator's console and which SIMH, dubna and
 * b6sim alike treat as "the run is over" (doc/Besm6_Instruction_Set.md).  So idle() is a
 * spin, and the `idling' flag is what gives it an exit: extintr() clears the flag after
 * servicing anything at all, so the spin ends on the first interrupt.  If none ever
 * arrives it spins forever -- but at spl0, with МГРП armed and the interval timer
 * free-running at HZ, one always does.
 *
 * The flag is also how clock() charges a tick to idle time rather than to the kernel.  It
 * has to be a flag and not a pc comparison: a pc test would have to be calibrated against
 * what the hardware saves (`tr->ret' is the raw saved IRET, and the saved pc does not name
 * the instruction you would expect -- see trap()), whereas a flag is exact and cannot drift
 * when the code around it is recompiled.
 *
 * Opening the door is a clear of БлПр and nothing else, which is why this is C and not
 * besm6.S.  The mode word is banked by hand first, since idle() must put the caller's level
 * back and spl0() is void -- see the comment over it.  That `ита'/`ати' pair is what an
 * s = spl6()/splx(s) bracket is anyway; idle() writes it out only because the level it wants
 * is 0 and spl0() hands nothing back.
 */
volatile int idling; /* set while the idle spin is running; cleared by extintr() */

void idle(void)
{
    int s = __besm6_getpsw();

    spl0();
    idling = 1;
    while (idling)
        ; /* spin */
    splx(s);
}

/*
 * Let a device's ПРП bits through.  Called from a driver's init, at any spl.
 *
 * Drivers must come through here rather than write МПРП themselves: the register is
 * a full 24-bit overwrite and cannot be read back, so a driver writing its own bits
 * would drop every other device's.
 */
void mprpon(unsigned bits)
{
    int s;

    s = spl7();
    mprp |= bits;
    __besm6_ext(EXT_MPRP, mprp);
    splx(s);
}

/*
 * Let a device's ГРП bits through, and shut them off again.  Same contract as mprpon()
 * and the same reason for existing -- МГРП is a write-only 48-bit overwrite with no read
 * address, so a driver writing its own bits would drop every other device's.
 *
 * Unlike ПРП, these come in PAIRS around one exchange.  A mass-storage completion bit is
 * WIRED and means "the channel is idle", so it stands whenever no transfer is running:
 * armed outside an exchange it would fire immediately, again, and forever, and extintr()
 * cannot dismiss it (only a new command to the device can).  So a driver arms its bit
 * after issuing the control word -- issuing it is what lowers the bit -- and disarms it in
 * the handler, before iodone().  See doc/Besm6_Peripherals.md, "Wired bits".
 */
void mgrpon(unsigned bits)
{
    int s;

    s = spl7();
    mgrp |= bits;
    __besm6_mod(MOD_MGRP, mgrp);
    splx(s);
}

void mgrpoff(unsigned bits)
{
    int s;

    s = spl7();
    mgrp &= ~bits;
    __besm6_mod(MOD_MGRP, mgrp);
    splx(s);
}

/*
 * A peripheral interrupt.  Ask every device that can raise one whether it was them;
 * each clears its own ПРП bits.  As more drivers are ported this grows a line each.
 */
static void prpintr(void)
{
    scintr();
}

/*
 * The external interrupt handler, called from the 0501 vector in besm6.S.
 *
 * Loop until nothing is pending: more than one device can be waiting, and a device
 * can raise a new interrupt while we are servicing another.
 *
 * THIS FUNCTION DOES NOT TOUCH THE INTERRUPT LEVEL, and does not have to.  v7's handlers raise
 * the ipl and let the return from interrupt drop it -- which is what the PDP-11's `rtt' does
 * when it reloads the priority field of PS from the frame, and what `выпр' does here too, БлПр
 * being the priority (see the spls above).  clock() calls spl5()/spl1() and restores neither;
 * the gate return puts the interrupted context's level back over the top of that, from SPSW.
 *
 * It used to end with `curipl = s', repairing a software shadow of the level that the handlers
 * had moved and the hardware knew nothing about.  The shadow is gone -- PSW reads back, so the
 * bit itself is the only copy -- and the repair went with it.  What that repair could NOT be
 * is worth keeping, since the reasoning still binds anything tempted to add one: not splx(),
 * which would put delivery back the way the interrupted context had it, i.e. OPEN, here inside
 * the handler with that context still in SPSW/IRET -- and the interval timer free-runs, so the
 * next tick would re-enter immediately and keep doing so.  Nothing in the loop wants delivery
 * open: every handler it calls only ever SETS БлПр, and `grp & mgrp' does not depend on the
 * level at all -- МГРП is a source mask, not a priority.
 */
void extintr(void)
{
    unsigned grp, bit;

    for (;;) {
        grp = __besm6_mod(MOD_GRP, 0) & mgrp;
        if (grp == 0)
            break;

        if (grp & GRP_SLAVE) {
            prpintr();                           /* clears the ПРП bits ... */
            __besm6_mod(MOD_GRPCLR, ~GRP_SLAVE); /* ... only then this */
            continue;
        }
        if (grp & GRP_TIMER) {
            /*
             * Dismissed BEFORE the handler -- the opposite of GRP_SLAVE above, and for
             * the opposite reason.  GRP_SLAVE is level-driven off ПРП and re-raises the
             * instant it is cleared with a ПРП bit still up.  GRP_TIMER is a plain
             * flip-flop (not one of GRP_WIRED_BITS), and the timer free-runs: clearing
             * it afterwards would erase a tick that arrived while clock() was running.
             */
            __besm6_mod(MOD_GRPCLR, ~GRP_TIMER);
            clock((struct trap *)intrframe);
            continue;
        }
        if (grp & (GRP_DRUM1_FREE | GRP_DRUM2_FREE)) {
            /*
             * A drum exchange finished.  Nothing is dismissed here and nothing can be:
             * these are WIRED bits, and the only thing that lowers one is the next
             * control word the driver issues -- or, when there is no next one,
             * mgrpoff() taking the bit out of МГРП.  mbintr() does both (kernel/dev/mb.c).
             * One drum at a time, so it needs no argument to tell it which.
             */
            mbintr();
            continue;
        }
        if (grp & (GRP_CHAN3_FREE | GRP_CHAN4_FREE)) {
            /*
             * A disk exchange finished.  Wired in the same way as the drum bits above, and
             * dismissed the same way -- by the next control word mdintr() causes to be
             * issued, or by its mgrpoff() when there is no next one (kernel/dev/md.c).
             * One controller at a time, so it needs no argument to tell it which.
             */
            mdintr();
            continue;
        }
        /*
         * Anything else: get rid of the highest pending bit -- anx numbers from the top
         * of the word, 1 = bit 48 -- and go round again, so that a bit nobody handles
         * cannot spin here forever.
         *
         * Dismissing it is not always possible.  The wired bits of ГРП are live wires
         * from the device rather than flip-flops, and MOD_GRPCLR silently does nothing to
         * them: the hardware clears only `GRP &= ACC | GRP_WIRED_BITS'.  Clearing one and
         * looping is the forever this arm exists to prevent.
         *
         * So probe rather than tabulate: clear it, read ГРП back, and if the bit is still
         * standing take it out of МГРП instead.  A table of wired bits would have to be
         * kept in step with the machine (there are eleven, and this kernel can raise four
         * -- see sys/besm6dev.h), and it would still miss a level-driven source whose
         * device nobody cleared.  The probe catches both, and it costs one extra read on
         * a path that is already the path where something has gone wrong.
         */
        bit = (unsigned)1 << (48 - __besm6_anx(grp, 0));
        __besm6_mod(MOD_GRPCLR, ~bit);
        if (__besm6_mod(MOD_GRP, 0) & bit)
            mgrpoff(bit);
    }

    /*
     * Release idle()'s spin.  Any interrupt will do, so the flag is cleared here rather
     * than in any particular handler.  AFTER the loop, not before it: clock() is called from inside
     * the loop and reads `idling' to charge the tick to idle time, so clearing it first
     * would book every idle tick as system time instead.
     */
    idling = 0;
}
