// The host-vs-native size agreement fixture (task C9c).
//
// size prints six numbers -- const, text, data, bss, their sum in decimal and
// the same sum in octal -- so the fixture's whole job is to make all four
// segments NONZERO AND DIFFERENT from one another.  Equal segments would let a
// transposition of two columns pass unnoticed, and a zero one would hide a
// column that never got read at all.
//
// The sizes below are 3 const words, 4 text words, 5 data words and 6 bss.

        .globl  entry

// ---- const: 3 words ----
        .const
ctable:
        .word   0x111111111111
        .word   0x222222222222
cdatum:
        .word   0x333333333333

// ---- text: 4 words ----
        .text
entry:
        xta     ctable
        a+x     cdatum
        atx     d1
        stop    012

// ---- data: 5 words ----
        .data
d1:
        .word   0
        .word   1
        .word   2
        .word   entry                   // a text address, so the segment relocates
        .word   cdatum                  // ... and a const one

// ---- bss: 6 words ----
        .bss
b1:
        . = . + 6
