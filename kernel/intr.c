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
 * Interrupt priority level.
 *
 * The BESM-6 has no priority hierarchy: an external interrupt fires whenever
 * GRP & MGRP is non-zero, and МГРП is the only knob there is.  So rather than
 * pretend to the eight graded levels of the PDP-11 and the x86 port, this kernel has
 * exactly two -- interrupts enabled and interrupts blocked.  Only spl0 enables;
 * every splN above it blocks.  Callers keep the v7 spelling (`s = spl5(); ...;
 * splx(s);`) and still get what they were really after: on a uniprocessor with no
 * atomic instruction, masking interrupts is the only lock in the machine.
 *
 * IRQ_ON is the set of sources this kernel can currently service.  GRP_TIMER joins
 * it once clock() can be called -- see extintr().  It is unsigned because ГРП is 48
 * bits wide and a signed int on this target holds only 41 of them.
 */
#define IRQ_ON GRP_SLAVE

static int curipl = 1; /* the machine comes up with interrupts blocked */

unsigned mgrp; /* shadow of МГРП: it cannot be read back */
unsigned mprp; /* shadow of МПРП: likewise */

static int setipl(int s)
{
    int old;

    old    = curipl;
    curipl = s;
    mgrp   = s ? 0 : IRQ_ON;
    __besm6_mod(MOD_MGRP, mgrp);
    return old;
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
 */
void extintr(void)
{
    unsigned grp;

    while ((grp = __besm6_mod(MOD_GRP, 0) & mgrp) != 0) {
        if (grp & GRP_SLAVE) {
            prpintr();                           /* clears the ПРП bits ... */
            __besm6_mod(MOD_GRPCLR, ~GRP_SLAVE); /* ... only then this */
            continue;
        }
        /*
         * TODO: GRP_TIMER belongs to clock(), but clock() takes a struct trap by
         * value and that frame is still the x86 one (include/sys/reg.h).  Building
         * the BESM-6 trap frame is part of the trap/besm6.S work; until it exists the
         * timer is left out of IRQ_ON, so it never gets here.
         *
         * Anything else: dismiss the highest pending bit -- anx numbers from the top
         * of the word, 1 = bit 48 -- and go round again, so that a bit nobody handles
         * cannot spin here forever.
         */
        __besm6_mod(MOD_GRPCLR, ~((unsigned)1 << (48 - __besm6_anx(grp, 0))));
    }
}
