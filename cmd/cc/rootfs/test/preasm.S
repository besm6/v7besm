// The preprocessed-assembly half of the cc agreement fixture (task C9e).
//
// A capital .S is the one input suffix that runs TWO stages, cpp then as, and on
// the machine it is the only multi-stage path the driver has left: the .c path
// stops after cpp, the compiler proper not being here.  So this file is written to
// give the preprocessor real work -- an object-like macro, a function-like one and
// a conditional -- and to leave behind assembly that b6as reads with the `# line'
// markers cpp interleaves through it.
//
// IT IS NOT NAMED agree.S, and that is this project's own hazard rather than a
// preference: the tree lives on a case-insensitive filesystem, where agree.S and
// agree.s are one file.  cc's same_file() guard exists for the same reason (see
// compile_one), and two fixtures differing only in case could not both be checked
// in here.
//
// `besm6' is predefined by b6cpp in BOTH builds -- it is what every size profile in
// cmd/ keys on -- so the arm taken is the same on either side; the #else exists to
// prove the conditional was evaluated rather than passed through.

// EXTRA is defined by nothing here: it is what the `defines' case passes with -D,
// and inc/extra.h is what its -I finds.  The other cases take neither arm, which is
// the point -- one fixture, two preprocessor outcomes, and the two sides have to
// agree on both.
#ifdef EXTRA
#   include <extra.h>
#endif

#define MAGIC     0x515151515151
#define SECOND(x) ((x) + 1)

        .globl  preasm_main, preasm_data

        .const
preasm_const:
        .word   MAGIC
        .word   SECOND(6)
#ifdef EXTRA
        .word   EXTRA_WORD + EXTRA
#endif

        .text
preasm_main:
        xta     preasm_const
        atx     preasm_data
#ifdef besm6
        stop    012
#else
        stop    077
#endif

        .data
preasm_data:
        .word   0
