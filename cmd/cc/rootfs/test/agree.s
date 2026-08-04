// The assembly half of the host-vs-native cc agreement fixture (task C9e).
//
// What the driver is under test for is the COMMAND LINES it builds, not the bytes
// any one stage emits -- b6as and b6ld already agree with their native selves,
// task C9b having settled that.  So this file only has to reach every segment and
// define a global, so that the object cc produces from it is one whose header and
// symbol table would show any difference in what the assembler was told.
//
// It is linked as well as assembled (the `link' case), and there it stands in for
// the program: it defines main, which is what crt0.o calls, and nothing else.  It
// is never RUN -- `stop' below is there to end the block, not to be reached -- so
// main neither takes its arguments nor returns through r13.

        .globl  main, agree_data

// ---- const ----
        .const
agree_const:
        .word   0x424242424242

// ---- text ----
        .text
main:
        xta     agree_const
        atx     agree_data
        stop    012

// ---- data ----
        .data
agree_data:
        .word   0

// ---- bss ----
        .bss
agree_bss:
        . = . + 3
