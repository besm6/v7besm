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
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/besm6dev.h"
// clang-format on

#include <besm6.h>

void scintr(void);

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
 * of the PDP-11 and the x86 port, this kernel has exactly two -- interrupts enabled and
 * interrupts blocked.  Only spl0 enables; every splN above it blocks.  Callers keep the v7
 * spelling (`s = spl5(); ...; splx(s);`) and still get what they were really after: on a
 * uniprocessor with no atomic instruction, masking interrupts is the only lock there is.
 *
 * TWO REGISTERS, TWO JOBS.  Delivery needs БлПр clear AND `ГРП & МГРП' non-zero, so either
 * could serve as the mask.  They are not interchangeable, and this kernel divides them:
 *
 *   БлПр (ПСВ bit 02000) is the PRIORITY.  setipl() sets it to block and clears it to
 *   enable, through cli()/sti() in besm6.S.  It is the right choice because the hardware
 *   already treats it as one: an interrupt or extracode forces БлПр on at the vector and
 *   `выпр' restores it from СПСВ, so a gate return re-establishes the level by itself --
 *   exactly what the PDP-11's `rtt' does when it reloads the priority field of PS.  МГРП,
 *   being a separate write-only register outside the mode word, does nothing of the kind.
 *
 *   МГРП is the SOURCE ENABLE, armed once by intrinit() with IRQ_ON and never rewritten.
 *   extintr()'s `grp & mgrp' then means "sources this kernel can service", which is what
 *   that mask was always for.
 *
 * It used to be the other way round -- setipl() rewrote МГРП and nothing ever touched БлПр
 * -- and the two mechanisms fought: the gates hold БлПр from the vector, so spl0() opened
 * МГРП while БлПр still blocked everything, and no interrupt could be taken in kernel mode
 * at all.  That was invisible while the only interrupts arrived in user mode, and became
 * load-bearing the moment idle() needed to spin waiting for one.
 *
 * IRQ_ON is the set of sources this kernel can currently service; it grows a bit per
 * driver.  It is unsigned because ГРП is 48 bits wide and a signed int on this target
 * holds only 41 of them.
 */
#define IRQ_ON (GRP_SLAVE | GRP_TIMER)

static int curipl = 1; /* the machine comes up with interrupts blocked */

unsigned mgrp; /* shadow of МГРП: it cannot be read back */
unsigned mprp; /* shadow of МПРП: likewise */

static int setipl(int s)
{
    int old;

    old    = curipl;
    curipl = s;
    if (s)
        cli(); /* set БлПр   -- external interrupts blocked */
    else
        sti(); /* clear БлПр -- delivery enabled */
    return old;
}

/*
 * Arm the interrupt sources this kernel can service.  Called once from main(), before the
 * first spl0(); after that МГРП never changes and the priority rides on БлПр alone.
 *
 * Separate from clkstart() because it is not the clock's business: every driver's sources
 * are in IRQ_ON, and a driver added later grows that constant rather than writing МГРП.
 */
void intrinit(void)
{
    mgrp = IRQ_ON;
    __besm6_mod(MOD_MGRP, mgrp);
}

void splx(int s)
{
    setipl(s);
}

int spl0(void)
{
    return setipl(0);
}

int spl1(void)
{
    return setipl(1);
}

int spl4(void)
{
    return setipl(4);
}

int spl5(void)
{
    return setipl(5);
}

int spl6(void)
{
    return setipl(6);
}

int spl7(void)
{
    return setipl(7);
}

/*
 * The idle loop, called from swtch() when nothing is runnable (and from panic()).
 *
 * THERE IS NO WAIT-FOR-INTERRUPT ON THIS MACHINE.  The only halt is 033 (стоп), which is
 * resumable on real hardware only from the operator's console and which SIMH, dubna and
 * b6sim alike treat as "the run is over" (doc/Besm6_Instruction_Set.md).  So this is a
 * spin, and the flag is what turns it back into `hlt': extintr() clears `idling' after
 * servicing anything at all, so the spin exits on the first interrupt, exactly as the x86
 * original's `hlt' did.  Like `hlt', it spins forever if none ever arrives -- at spl0, with
 * МГРП armed and the interval timer free-running at HZ, one always does.
 *
 * The flag also replaces x86's `waitloc'.  That was the address of the idle `hlt', which
 * clock() compared against the interrupted pc to charge the tick to idle rather than to the
 * kernel; there is no halt instruction here to point at, and a pc comparison would have to
 * be calibrated against what the hardware saves (`tr->ret' is the raw saved IRET, and the
 * saved pc does not name the instruction you would expect -- see trap()).  A flag is exact
 * and cannot drift when the code around it is recompiled.
 *
 * spl0() is what actually opens the door, by clearing БлПр; there are no mode bits left for
 * this function to poke, which is why it is C and not besm6.S.
 */
volatile int idling; /* set while the idle spin is running; cleared by extintr() */

void idle(void)
{
    int s;

    s      = spl0();
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
 * What this function owns, on behalf of every handler it calls, is the interrupt priority
 * level.  v7's handlers raise the ipl and let the return from interrupt drop it -- which is
 * what the PDP-11's `rtt' does when it reloads the priority field of PS from the frame, and
 * what `выпр' now does here too, БлПр being the priority (see setipl above).  So the
 * HARDWARE bit needs nothing from us.  `curipl', the software shadow, does: clock() calls
 * spl5()/spl1() and restores neither, so without the repair at the bottom it would drift out
 * of step with БлПр from the very first tick on.
 *
 * The repair is a plain assignment, NOT splx().  splx(0) would call sti() and clear БлПр
 * here, inside the handler, with the interrupted context's state still in СПСВ/IRET -- and
 * the interval timer free-runs, so the next tick would re-enter this function immediately
 * and keep doing so.  Nothing in the loop below wants delivery open, either: every handler
 * it calls raises the level (spl5/spl1, both of which only ever SET БлПр), and `grp & mgrp'
 * no longer depends on the level at all now that МГРП is a constant source mask.  That is
 * also why the repair moved to the bottom -- it used to sit at the top of the loop precisely
 * because `& mgrp' could otherwise mask out a device that was pending all along.
 */
void extintr(void)
{
    int s = curipl;
    unsigned grp;

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
        /*
         * Anything else: dismiss the highest pending bit -- anx numbers from the top
         * of the word, 1 = bit 48 -- and go round again, so that a bit nobody handles
         * cannot spin here forever.
         */
        __besm6_mod(MOD_GRPCLR, ~((unsigned)1 << (48 - __besm6_anx(grp, 0))));
    }

    /* Put the shadow back where the handlers found it -- see the header above. */
    curipl = s;

    /*
     * Release idle()'s spin.  Any interrupt will do -- this is the machine's stand-in for
     * the `hlt' the x86 original woke from -- so it is cleared here rather than in any
     * particular handler.  AFTER the loop, not before it: clock() is called from inside
     * the loop and reads `idling' to charge the tick to idle time, so clearing it first
     * would book every idle tick as system time instead.
     */
    idling = 0;
}
