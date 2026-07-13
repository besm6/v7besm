/* V7/x86 source code: see www.nordier.com/v7x86 for details. */
/* Copyright (c) 2007 Robert Nordier.  All rights reserved. */

/*
 * System console driver: the BESM-6 Consul-254 operator typewriter.
 *
 * The Consul is a parallel, character-at-a-time device: one whole character goes
 * out per `ext` instruction, printing finished and character typed each raise an
 * interrupt.  That is a UART's shape, so this driver follows sr.c, not the CGA
 * screen driver this file used to be -- the Consul is a real terminal and does its
 * own cursor motion and scrolling, so there is no screen model here at all.
 *
 * Two paths reach the typewriter:
 *
 *   - the tty layer, through scstart()/scintr(): one character is handed to the
 *     device, BUSY is set, and the next one goes out when PRP_CONS1_DONE says the
 *     last has printed.
 *
 *   - printf() and panic(), through putchar(): polled, no interrupts, no clists.
 *     prf.c calls it with the rest of the system already stopped.
 *
 * The character codes are ASCII.  The authentic Consul code is GOST-10859, but it
 * has no lowercase Latin letters, which a Unix console cannot do without; the SIMH
 * line is therefore configured `raw` (see kernel/test/sctest.ini) and the bytes go
 * out untranslated.  Because nothing then translates line endings for us either,
 * the tty must run with CRMOD -- see scopen().
 *
 * doc/Besm6_Peripherals.md has the register map, doc/Intrinsics.md the intrinsics.
 * kernel/test/sctest.c exercises both paths of this driver against SIMH.
 */

// clang-format off
#include "sys/param.h"
#include "sys/conf.h"
#include "sys/dir.h"
#include "sys/user.h"
#include "sys/tty.h"
#include "sys/systm.h"
#include "sys/besm6dev.h"
// clang-format on

#include <besm6.h>

/*
 * The console is Consul 1.  Consul 2 exists (EXT_CONS2, PRP_CONS2_*, CONS2_READY)
 * but nothing in this kernel has a second console to put on it.
 */
#define SC_PRINT EXT_CONS1
#define SC_READ  EXT_CONS1_RD
#define SC_READY CONS1_READY
#define SC_INPUT PRP_CONS1_INPUT
#define SC_DONE  PRP_CONS1_DONE

/*
 * How long putchar() waits for the typewriter.  The ready bit is re-raised from the
 * simulated terminal's clock, so a character costs milliseconds of model time; this
 * only has to be larger than that, and finite so that a dead console cannot wedge a
 * panic.
 */
#define SC_SPIN 1000000

struct tty sc;

char msgbuf[MSGBUFS]; /* saved "printf" characters */
char *msgbufp = msgbuf;

void scinit(void);
void scintr(void);
void scstart(struct tty *tp);

void scopen(dev_t dev, int flag)
{
    register struct tty *tp;

    if (minor(dev) > 0) {
        u.u_error = ENXIO;
        return;
    }
    tp          = &sc;
    tp->t_addr  = (caddr_t)0;
    tp->t_oproc = scstart;
    if ((tp->t_state & ISOPEN) == 0) {
        tp->t_state = ISOPEN | CARR_ON;
        /*
         * CRMOD is what makes ttyoutput() send CR before NL and ttyinput() turn a
         * typed CR into NL.  The line is raw on the simulator side, so if the
         * kernel does not do this nothing will.
         */
        tp->t_flags = ECHO | CRMOD | XTABS;
        ttychars(tp);
        scinit();
    }
    ttyopen(dev, tp);
}

void scclose(dev_t dev, int flag)
{
    ttyclose(&sc);
}

void scread(dev_t dev)
{
    ttread(&sc);
}

void scwrite(dev_t dev)
{
    ttwrite(&sc);
}

void scioctl(dev_t dev, int command, caddr_t addr, int flag)
{
    if (ttioccomm(command, &sc, addr, dev) == 0)
        u.u_error = ENOTTY;
}

/*
 * Enable the Consul's interrupts.
 *
 * The machine comes up with a "printing finished" already pending, so dismiss both
 * of our bits before unmasking, or the first interrupt would arrive before the
 * first character does.  МГРП is not touched here: GRP_SLAVE, which is how any ПРП
 * interrupt reaches the processor, belongs to spl().
 */
void scinit(void)
{
    __besm6_ext(EXT_PRPCLR, ~(unsigned)(SC_INPUT | SC_DONE));
    mprpon(SC_INPUT | SC_DONE);
}

/*
 * Hand the typewriter one character.  Called from the tty layer with the output
 * queue non-empty, and again from scintr() as each character finishes printing.
 */
void scstart(register struct tty *tp)
{
    register int c;

    if (tp->t_state & (TIMEOUT | BUSY | TTSTOP))
        return;
    if ((c = getc(&tp->t_outq)) >= 0) {
        if (c >= 0200 && (tp->t_flags & RAW) == 0) {
            /* A delay, not a character: wait it out and come back. */
            tp->t_state |= TIMEOUT;
            timeout(ttrstrt, (caddr_t)tp, (c & 0177) + 6);
        } else {
            tp->t_char = c;
            tp->t_state |= BUSY;
            __besm6_ext(SC_PRINT, c & 0177);
        }
        if (tp->t_outq.c_cc <= TTLOWAT && tp->t_state & ASLEEP) {
            tp->t_state &= ~ASLEEP;
            wakeup((caddr_t)&tp->t_outq);
        }
    }
}

/*
 * Consul interrupt, from the ПРП demux in intr.c.
 *
 * Each bit is dismissed by writing its complement -- a ZERO bit in the accumulator
 * is what clears a ПРП bit, so the ones left standing preserve every other device's
 * bits.  The caller dismisses GRP_SLAVE only after this returns; doing it the other
 * way round would re-raise GRP_SLAVE immediately, because the processor re-tests
 * ПРП before every instruction.
 *
 * A "printing finished" with BUSY already clear is normal -- putchar() prints
 * behind the tty layer's back and its character raises the bit too.  ttstart() then
 * finds the queue empty and does nothing.
 */
void scintr(void)
{
    register struct tty *tp = &sc;
    unsigned prp;

    prp = __besm6_ext(EXT_PRPLO, 0);
    if (prp & SC_INPUT) {
        __besm6_ext(EXT_PRPCLR, ~(unsigned)SC_INPUT);
        /* Bits 1-7 are the character; bit 8 is odd parity, not data. */
        ttyinput(__besm6_ext(SC_READ, 0) & 0177, tp);
    }
    if (prp & SC_DONE) {
        __besm6_ext(EXT_PRPCLR, ~(unsigned)SC_DONE);
        tp->t_state &= ~BUSY;
        ttstart(tp);
    }
}

/*
 * Print one character by polling, with no help from the interrupt path: wait for the
 * typewriter to go idle, hand it the character, and wait for that one to print too.
 *
 * Leaving the device idle on return is what lets this coexist with scstart(): the
 * interrupt path finds the hardware exactly as it left it, and the spurious "printing
 * finished" this character raises is absorbed by scintr()'s tolerance of a DONE with
 * BUSY clear.  The caller holds spl7, so scintr() cannot cut in between the two waits.
 */
static void scputc(int c)
{
    int i;

    for (i = SC_SPIN; i; i--) /* has the previous character printed? */
        if (__besm6_ext(EXT_READY2, 0) & SC_READY)
            break;
    __besm6_ext(SC_PRINT, c & 0177);
    for (i = SC_SPIN; i; i--) /* has ours? */
        if (__besm6_ext(EXT_READY2, 0) & SC_READY)
            break;
}

/*
 * Kernel printf output ends up here.  It is buffered for later retrieval by dmesg,
 * and printed on the console -- polled, because printf() and panic() must work with
 * the scheduler and the tty layer dead.
 */
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
        scputc('\r'); /* the tty layer is not in this path to do it for us */
    scputc(c);
    splx(s);
}
