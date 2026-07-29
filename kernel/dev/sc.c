// Terminal driver: the BESM-6 Consul-254 operator typewriters, both of them.
//
// The Consul is a parallel, character-at-a-time device: one whole character goes
// out per `ext' instruction, printing finished and character typed each raise an
// interrupt.  That is a UART's shape, so this driver is an ordinary tty driver --
// the Consul is a real terminal and does its own cursor motion and scrolling, so
// there is no screen model here at all.
//
// THE MACHINE HAS TWO OF THEM and this file drives both, as cdevsw[0] minors 0 and
// 1: /dev/console (also /dev/tty0) is Consul 1, and /dev/tty1 is Consul 2, the
// second terminal the multiuser work needs (kernel/TODO.md task 29).  One driver
// rather than two because the hardware asked for it -- see the register macros
// below -- and because one interrupt handler then reads ПРП once for both lines.
//
// Three paths reach a typewriter:
//
//   - the tty layer, through scstart()/scintr(): one character is handed to the
//     device, BUSY is set, and the next one goes out when that line's DONE bit
//     says the last has printed.
//
//   - printf() and panic(), through putchar(): polled, no interrupts, no clists.
//     prf.c calls it with the rest of the system already stopped.  UNIT 0 ONLY --
//     a panic has exactly one place to go.
//
//   - scintr()'s receive arm, which hands a typed character to ttyinput().
//
// The character codes are ASCII.  The authentic Consul code is GOST-10859, but it
// has no lowercase Latin letters, which a Unix console cannot do without; the SIMH
// line is therefore configured `raw' (see kernel/test/sctest.ini) and the bytes go
// out untranslated.  Because nothing then translates line endings for us either,
// the tty must run with CRMOD -- see scopen().
//
// WHAT IS AND IS NOT TESTED.  kernel/test/sctest exercises both Consuls at the
// device level -- the polled path, the interrupt path, and that the two lines do
// not steal each other's ПРП bits or ready bits.  Above that, only unit 0 is
// covered: every booting test types at /dev/console.  The tty-layer path through
// scopen(dev, 1) and ttwrite() on minor 1 is first exercised by task 29c, which is
// the multiuser test; until then /dev/tty1 has no getty on it and nothing opens it.
//
// doc/Besm6_Peripherals.md has the register map, doc/Intrinsics.md the intrinsics.

// clang-format off
#include "sys/types.h"
#include "sys/param.h"
#include "sys/conf.h"
#include "sys/dir.h"
#include "sys/user.h"
#include "sys/tty.h"
#include "sys/systm.h"
#include "sys/besm6dev.h"
// clang-format on

#include <besm6.h>

// The two typewriters, as this driver's unit numbers: 0 is Consul 1, 1 is Consul 2.
#define NSC 2

// Per-unit registers and bits.  THE HARDWARE NUMBERING IS WHY THIS IS ONE DRIVER:
// EXT_CONS1/EXT_CONS2 are ADJACENT addresses and each ПРП pair is one bit apart, so
// a variable unit folds into the instruction's own address field with a single `wtc'
// beside it -- the case doc/Intrinsics.md §8 and the header block of sys/besm6dev.h
// both describe, and the one dev/mb.c uses for its two drum controllers.  (Operand
// order matters there; see mb.c.)
//
// READY2 does NOT follow the pattern: Consul 2 is bit 6, not bit 7, so the ready bits
// are a table rather than a shift (sys/besm6dev.h, "Bits of READY2").
#define SC_PRINT(u) (EXT_CONS1 + (u))
#define SC_READ(u)  (EXT_CONS1_RD + (u))
#define SC_INPUT(u) (PRP_CONS1_INPUT >> (u))
#define SC_DONE(u)  (PRP_CONS1_DONE >> (u))

static const int scready[NSC] = { CONS1_READY, CONS2_READY };

// How long putchar() waits for the typewriter.  The ready bit is re-raised from the
// simulated terminal's clock, so a character costs milliseconds of model time; this
// only has to be larger than that, and finite so that a dead console cannot wedge a
// panic.
#define SC_SPIN 1000000

struct tty sc[NSC];

char msgbuf[MSGBUFS]; // saved "printf" characters
char *msgbufp = msgbuf;

void scinit(int unit);
void scintr(void);
void scstart(struct tty *tp);

void scopen(dev_t dev, int flag)
{
    register struct tty *tp;
    register int unit;

    unit = minor(dev);
    if (unit >= NSC) {
        u.u_error = ENXIO;
        return;
    }
    tp = &sc[unit];
    // t_addr is the LINE NUMBER, not an address -- this machine has no device
    // address space (sys/tty.h).  scstart() turns it back into a register number.
    tp->t_addr  = unit;
    tp->t_oproc = scstart;
    if ((tp->t_state & ISOPEN) == 0) {
        tp->t_state = ISOPEN | CARR_ON;
        // CRMOD is what makes ttyoutput() send CR before NL and ttyinput() turn a
        // typed CR into NL.  The line is raw on the simulator side, so if the
        // kernel does not do this nothing will.
        //
        // ECHO on BOTH lines: the simulator does no local echo on the Consul path
        // (only its serial multiplexer echoes), so every character a user sees is
        // one this kernel put there.
        tp->t_flags = ECHO | CRMOD | XTABS;
        ttychars(tp);
        scinit(unit);
    }
    ttyopen(dev, tp);
}

void scclose(dev_t dev, int flag)
{
    ttyclose(&sc[minor(dev)]);
}

void scread(dev_t dev)
{
    ttread(&sc[minor(dev)]);
}

void scwrite(dev_t dev)
{
    ttwrite(&sc[minor(dev)]);
}

void scioctl(dev_t dev, int command, caddr_t addr, int flag)
{
    if (ttioccomm(command, &sc[minor(dev)], addr, dev) == 0)
        u.u_error = ENOTTY;
}

// Enable one Consul's interrupts, at its first open.
//
// The machine comes up with a "printing finished" already pending -- for BOTH
// typewriters -- so dismiss this line's two bits before unmasking, or the first
// interrupt would arrive before the first character does.  Per unit, and only the
// unit's own bits: the second Consul's pending DONE simply sits in ПРП, masked and
// harmless, until /dev/tty1 is first opened.  МГРП is not touched here: GRP_SLAVE,
// which is how any ПРП interrupt reaches the processor, belongs to spl().
void scinit(int unit)
{
    __besm6_ext(EXT_PRPCLR, ~(unsigned)(SC_INPUT(unit) | SC_DONE(unit)));
    mprpon(SC_INPUT(unit) | SC_DONE(unit));
}

// Hand the typewriter one character.  Called from the tty layer with the output
// queue non-empty, and again from scintr() as each character finishes printing.
void scstart(register struct tty *tp)
{
    register int c;

    if (tp->t_state & (TIMEOUT | BUSY | TTSTOP))
        return;
    if ((c = getc(&tp->t_outq)) >= 0) {
        if (c >= 0200 && (tp->t_flags & RAW) == 0) {
            // A delay, not a character: wait it out and come back.
            //
            // The count came out of ttyoutput()'s delay table (dev/tty.c), which is
            // v7's and is written in SIXTIETHS -- the PDP-11's tick.  timeout() counts
            // in this machine's ticks, HZ of them to the second, so the table's numbers
            // have to be scaled here or every delay is four times too short.  The 60 is
            // v7's clock rate, not a minute; scaling at the two consumers keeps the
            // table itself legible as the v7 table it is.
            //
            // Unreachable as the console is opened today: scopen() sets ECHO|CRMOD|XTABS
            // and every arm of that table reads a delay field this leaves zero, so `c'
            // is never non-zero and tty.c never queues a delay byte.  A program that
            // sets the delay bits through TIOCSETP reaches it.
            tp->t_state |= TIMEOUT;
            timeout(ttrstrt, (carg_t)tp, ((c & 0177) + 6) * HZ / 60);
        } else {
            tp->t_char = c;
            tp->t_state |= BUSY;
            __besm6_ext(SC_PRINT(tp->t_addr), c & 0177);
        }
        if (tp->t_outq.c_cc <= TTLOWAT && tp->t_state & ASLEEP) {
            tp->t_state &= ~ASLEEP;
            wakeup((chan_t)&tp->t_outq);
        }
    }
}

// Consul interrupt, from the ПРП demux in intr.c.  ONE HANDLER FOR BOTH LINES:
// ПРП is read once and every unit's pair of bits tested against that one word,
// which is the whole reason the second typewriter is not a driver of its own.
//
// Each bit is dismissed by writing its complement -- a ZERO bit in the accumulator
// is what clears a ПРП bit, so the ones left standing preserve every other device's
// bits, the other Consul's included.  The caller dismisses GRP_SLAVE only after this
// returns; doing it the other way round would re-raise GRP_SLAVE immediately, because
// the processor re-tests ПРП before every instruction.  A bit that goes up while the
// other unit is being served is caught by extintr()'s next pass round its loop.
//
// A "printing finished" with BUSY already clear is normal -- putchar() prints
// behind the tty layer's back and its character raises the bit too.  ttstart() then
// finds the queue empty and does nothing.
void scintr(void)
{
    register struct tty *tp;
    register int unit;
    unsigned prp;

    prp = __besm6_ext(EXT_PRPLO, 0);
    for (unit = 0; unit < NSC; unit++) {
        tp = &sc[unit];
        // A LINE NOBODY HAS OPENED HAS NOTHING TO DO, and this is not belt and
        // braces.  ПРП is read live here, not through МПРП, so a bit that is masked
        // is still SEEN: the machine comes up with "printing finished" standing for
        // both typewriters, scinit() dismisses only the bits of the line being
        // opened, and Consul 2's therefore stands from reset until /dev/tty1 is
        // first opened.  Without this test the first console interrupt of the boot
        // also "completed" Consul 2 and called ttstart() on a tty whose t_oproc is
        // still zero -- a jump to 0, which is GRP_INSN_CHECK and a kernel trap
        // before the root is even mounted.  kernel/test/sctest cannot see this: it
        // drives the hardware and does not link the driver.  kernel/test/boot did.
        if ((tp->t_state & ISOPEN) == 0)
            continue;
        if (prp & SC_INPUT(unit)) {
            __besm6_ext(EXT_PRPCLR, ~(unsigned)SC_INPUT(unit));
            // Bits 1-7 are the character; bit 8 is odd parity, not data.
            ttyinput(__besm6_ext(SC_READ(unit), 0) & 0177, tp);
        }
        if (prp & SC_DONE(unit)) {
            __besm6_ext(EXT_PRPCLR, ~(unsigned)SC_DONE(unit));
            tp->t_state &= ~BUSY;
            ttstart(tp);
        }
    }
}

// Print one character on Consul 1 by polling, with no help from the interrupt path:
// wait for the typewriter to go idle, hand it the character, and wait for that one to
// print too.  UNIT 0 IS NOT A PARAMETER HERE -- this is panic()'s path, and a panic
// has exactly one place to go.
//
// Leaving the device idle on return is what lets this coexist with scstart(): the
// interrupt path finds the hardware exactly as it left it, and the spurious "printing
// finished" this character raises is absorbed by scintr()'s tolerance of a DONE with
// BUSY clear.  The caller holds spl7, so scintr() cannot cut in between the two waits.
static void scputc(int c)
{
    int i;

    for (i = SC_SPIN; i; i--) // has the previous character printed?
        if (__besm6_ext(EXT_READY2, 0) & scready[0])
            break;
    __besm6_ext(SC_PRINT(0), c & 0177);
    for (i = SC_SPIN; i; i--) // has ours?
        if (__besm6_ext(EXT_READY2, 0) & scready[0])
            break;
}

// Kernel printf output ends up here.  It is buffered for later retrieval by dmesg,
// and printed on the console -- polled, because printf() and panic() must work with
// the scheduler and the tty layer dead.
void putchar(int c)
{
    int s;

    if (c == 0)
        return;
    if (c != '\r') {
        *msgbufp = c;
        if (++msgbufp >= &msgbuf[MSGBUFS])
            msgbufp = msgbuf;
    }
    s = spl7();
    if (c == '\n')
        scputc('\r'); // the tty layer is not in this path to do it for us
    scputc(c);
    splx(s);
}
