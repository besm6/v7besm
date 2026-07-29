// sctest.c -- exercise the BESM-6 Consul typewriters from C, using the <besm6.h>
// intrinsics, the way kernel/dev/sc.c does.
//
// The driver is not linked here: what is verified is the same `ext' instructions it
// issues, driven from compiled C against the real machine model, with no kernel
// underneath.  Three parts.
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
//   Part 3 -- THE TWO TYPEWRITERS ARE TWO DEVICES.  dev/sc.c drives both Consuls
//             from one interrupt handler that reads ПРП once, so the failure it can
//             have and no single-line test can see is one line being credited with
//             the other's interrupt, or dismissing the other's bit.  Part 3 runs a
//             message on each concurrently, of DIFFERENT LENGTHS so that a swapped
//             or shared counter cannot come out right, and asserts three things:
//             each line took exactly one interrupt per character, the ready bits
//             moved independently (CONS2_READY is bit 6, not bit 7 -- the one place
//             that irregularity is checked), and no byte crossed between the lines.
//
//             The last of those is the .ini's half: the two messages interleave, so
//             Consul 1 gets "acegik" while Consul 2 gets "bdfhjl", and sctest.ini
//             expects "acegik" on the console.  A byte that crossed would make the
//             console stream read "abcdefghijkl".
//
// CONSUL 2 IS DELIBERATELY NOT ATTACHED to anything, and part 3 still works: SIMH's
// consul_print() lowers READY2 and consul_receive() raises the "printing finished"
// bit whether or not the line is connected -- only the character itself is discarded,
// by vt_putc()'s connection check.  So every interrupt counted here really fires, and
// leaving line 26 unattached is what makes "no byte reached the console" assertable.
//
// main() returns 0 on success; crt0.s leaves that in the accumulator and halts, and
// sctest.ini asserts on it.  Characters are one per word, as in hello.s: the point
// here is the hardware path, not char packing.
//
// Run: make sctest && besm6 sctest.ini
#include <besm6.h>

#include "sys/besm6dev.h"

#define NSC 2 // Consul 1 and Consul 2

static const unsigned polled[] = { 'p', 'o', 'l', 'l', 'e', 'd', '\r', '\n', 0 };
static const unsigned onintr[] = { 'i', 'n', 't', 'e', 'r', 'r', 'u', 'p', 't', '\r', '\n', 0 };

// Part 3's two messages.  Different lengths on purpose -- with equal ones a handler
// that credited every interrupt to unit 0 would still leave both counters looking
// plausible.  Consul 1's is the odd letters of "abcdefghijkl" and Consul 2's the
// even ones, so the console stream tells whether a byte went out on the wrong line.
static const unsigned twoway[NSC][8] = {
    { 'a', 'c', 'e', 'g', 'i', 'k', '\r', 0 },
    { 'b', 'd', 'f', 'h', 'j', 'l', 0, 0 },
};

// Shared with extintr() below, which runs from the 0501 vector.
static volatile unsigned outp;  // how far through onintr[] we have got
static volatile unsigned ndone; // "printing finished" interrupts taken
static volatile unsigned busy;  // cleared by extintr() when the message is out

// Part 3's state, one entry per Consul.  `part3' switches extintr() over to the
// two-line arm; the parts do not run at the same time.
static volatile unsigned part3;
static volatile unsigned outp2[NSC];
static volatile unsigned ndone2[NSC];
static volatile unsigned busy2[NSC];

// The same per-unit derivation dev/sc.c uses: adjacent `ext' addresses, ПРП bits one
// apart, and a ready bit that follows neither.
#define SC_PRINT(u) (EXT_CONS1 + (u))
#define SC_INPUT(u) (PRP_CONS1_INPUT >> (u))
#define SC_DONE(u)  (PRP_CONS1_DONE >> (u))

static const unsigned scready[NSC] = { CONS1_READY, CONS2_READY };

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
    unsigned u;

    if (!(__besm6_mod(MOD_GRP, 0) & GRP_SLAVE))
        return;

    prp = __besm6_ext(EXT_PRPLO, 0);

    if (part3) {
        // The shape dev/sc.c's scintr() has: ПРП read once, every unit's bits
        // tested against that one word, each dismissed with its own complement.
        for (u = 0; u < NSC; u++) {
            if (prp & SC_DONE(u)) {
                __besm6_ext(EXT_PRPCLR, ~SC_DONE(u));
                ndone2[u]++;
                if (twoway[u][outp2[u]])
                    __besm6_ext(SC_PRINT(u), twoway[u][outp2[u]++]);
                else
                    busy2[u] = 0;
            }
        }
        __besm6_mod(MOD_GRPCLR, ~GRP_SLAVE);
        return;
    }

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

// Part 3.  Returns 0 on success, or a small non-zero code naming what went wrong so
// that the .ini's `ex ACC' says something.
static unsigned twoconsuls(void)
{
    unsigned u, n;

    // Both lines' pending bits, not just line 0's: SIMH's tty_reset raises
    // "printing finished" for BOTH Consuls.
    __besm6_ext(EXT_PRPCLR, ~(unsigned)(SC_INPUT(0) | SC_DONE(0) | SC_INPUT(1) | SC_DONE(1)));

    for (u = 0; u < NSC; u++) {
        outp2[u]  = 0;
        ndone2[u] = 0;
        busy2[u]  = 1;
    }
    part3 = 1;

    __besm6_ext(EXT_MPRP, SC_DONE(0) | SC_DONE(1));
    __besm6_mod(MOD_MGRP, GRP_SLAVE);

    // THE READY BITS ARE INDEPENDENT, and this asserts a TRANSITION rather than a
    // level -- a level is not enough, because a wrong bit number usually names a bit
    // that reads zero and a "must be clear" test passes on it by accident.  So: both
    // bits up while nothing is printing, then hand Consul 2 a character with Consul 1
    // still idle, and exactly Consul 2's bit must have gone down.  Each READY2 is read
    // once, so the two bits of a pair come from the same word.
    n = __besm6_ext(EXT_READY2, 0);
    if (!(n & scready[0]) || !(n & scready[1])) {
        part3 = 0;
        __besm6_mod(MOD_MGRP, 0);
        __besm6_ext(EXT_MPRP, 0);
        return 2;
    }
    __besm6_ext(SC_PRINT(1), twoway[1][outp2[1]++]);
    n = __besm6_ext(EXT_READY2, 0);
    if (!(n & scready[0]) || (n & scready[1])) {
        part3 = 0;
        __besm6_mod(MOD_MGRP, 0);
        __besm6_ext(EXT_MPRP, 0);
        return 2;
    }

    __besm6_ext(SC_PRINT(0), twoway[0][outp2[0]++]); // now both are running

    while (busy2[0] || busy2[1])
        ;

    part3 = 0;
    __besm6_mod(MOD_MGRP, 0);
    __besm6_ext(EXT_MPRP, 0);

    // Exactly one interrupt per character, per line, against that line's OWN length
    // -- 7 for Consul 1 and 6 for Consul 2, which is the point of their being
    // different.  (The kick prints the first character, each of the next N-1
    // interrupts prints one more, and the Nth finds the message exhausted and ends
    // the line.  Same arithmetic as part 2 above.)
    for (u = 0; u < NSC; u++) {
        for (n = 0; twoway[u][n]; n++)
            ;
        if (ndone2[u] != n)
            return 3 + u;
    }
    return 0;
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
    if (ndone != i)
        return 1;

    return twoconsuls();
}
