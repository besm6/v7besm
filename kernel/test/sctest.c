// sctest.c -- exercise both halves of the BESM-6 console (Consul typewriter 1)
// from C, using the <besm6.h> intrinsics, the way kernel/dev/sc.c does.
//
// The kernel does not boot on SIMH yet, so this is how the console primitives are
// verified: the same `ext` instructions the driver issues, driven from compiled C,
// against the real machine model.
//
//   Part 1 -- the polled path (what putchar()/printf()/panic() use).  Spin on the
//             Consul's ready bit in READY2, then hand it a character.  This is
//             hello.s written in C.
//
//   Part 2 -- the interrupt path (what the tty layer uses).  Unmask the Consul's
//             "printing finished" bit in МПРП and GRP_SLAVE in МГРП, hand the
//             typewriter the first character, and let the interrupt handler feed it
//             the rest, one character per interrupt.
//
// main() returns 0 on success; crt0.s leaves that in the accumulator and halts, and
// sctest.ini asserts on it.  Characters are one per word, as in hello.s: the point
// here is the hardware path, not char packing.
//
// Run: make sctest && besm6 sctest.ini
#include <besm6.h>

#include "sys/besm6dev.h"

static const unsigned polled[] = { 'p', 'o', 'l', 'l', 'e', 'd', '\r', '\n', 0 };
static const unsigned onintr[] = { 'i', 'n', 't', 'e', 'r', 'r', 'u', 'p', 't', '\r', '\n', 0 };

// Shared with extintr() below, which runs from the 0501 vector.
static volatile unsigned outp;  // how far through onintr[] we have got
static volatile unsigned ndone; // "printing finished" interrupts taken
static volatile unsigned busy;  // cleared by extintr() when the message is out

// The polled path.  The Consul lowers its ready bit while it prints and the
// hardware raises it again when the character is out, so spin before handing it
// one -- exactly what the loop in hello.s does.
static void scputc(unsigned c)
{
    while (!(__besm6_ext(EXT_READY2, 0) & CONS1_READY))
        ;
    __besm6_ext(EXT_CONS1, c);
}

// External interrupt handler, called by the stub at the 0501 vector in crt0.s.
//
// ПРП has no interrupt line: the processor raises GRP_SLAVE for as long as any
// unmasked ПРП bit is up, and re-tests it before every instruction.  So the ПРП bit
// must be cleared BEFORE GRP_SLAVE is dismissed -- the other order re-raises
// GRP_SLAVE immediately and the machine never leaves the handler.
//
// Both registers are cleared by writing a mask in which a ZERO bit clears, so what
// goes to the hardware is the complement of the bit being dismissed.
void extintr(void)
{
    unsigned prp;

    if (!(__besm6_mod(MOD_GRP, 0) & GRP_SLAVE))
        return;

    prp = __besm6_ext(EXT_PRPLO, 0);
    if (prp & PRP_CONS1_DONE) {
        __besm6_ext(EXT_PRPCLR, ~(unsigned)PRP_CONS1_DONE);
        ndone++;
        if (onintr[outp])
            __besm6_ext(EXT_CONS1, onintr[outp++]);
        else
            busy = 0;
    }
    __besm6_mod(MOD_GRPCLR, ~GRP_SLAVE);
}

int main(void)
{
    unsigned i;

    for (i = 0; polled[i]; i++)
        scputc(polled[i]);

    // Reset leaves a "printing finished" bit already pending (SIMH's tty_reset
    // raises CONS_CAN_PRINT for both Consuls), so dismiss it before unmasking or
    // the first interrupt arrives before the first character does.
    __besm6_ext(EXT_PRPCLR, ~(unsigned)(PRP_CONS1_INPUT | PRP_CONS1_DONE));

    outp  = 0;
    ndone = 0;
    busy  = 1;

    __besm6_ext(EXT_MPRP, PRP_CONS1_DONE);
    __besm6_mod(MOD_MGRP, GRP_SLAVE);

    __besm6_ext(EXT_CONS1, onintr[outp++]); // kick: the rest follows on interrupts

    while (busy)
        ;

    __besm6_mod(MOD_MGRP, 0);
    __besm6_ext(EXT_MPRP, 0);

    // One interrupt per character, plus the last one that found the message
    // exhausted and ended the loop.
    for (i = 0; onintr[i]; i++)
        ;
    return ndone != i;
}
