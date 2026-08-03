// Part two of the host-vs-native linker agreement fixture (task C9b).  See
// link1.s: between them every reference in one is defined by the other.
//
// The two "#expr" literals here are the SAME VALUES link1.s asks for, so the
// pool merges them and the const words this file thought it owned move.  That
// is what newindex[] is for, and this is the fixture that would catch the two
// builds disagreeing about it.

        .globl  shared2, shared3, b$save
        .comm   commblk, 4              // the same common block, requested twice

// ---- const, including one word only this file contributes ----
        .const
c2table:
        .word   0x444444444444
c2end:
        .word   c2table

// ---- text ----
        .text
shared2:
        its     13
     13 vjm     b$save
        xta     #0777000777000          // shared with link1.s -- merged away
        xta     #0123456                // ... likewise
        xta     #0555555555555          // this one is only ours
        xta     c2table                 // a direct const reference
        atx     <c2end>                 // the utc escape
        xta     entry                   // back-reference into link1.s
        xta     shared1                 // ... and to its datum
        atx     shared3
        xta     commblk
     13 uj

// ---- data ----
        .data
shared3:
        .word   0
        .word   shared2, c2table

// ---- a minimal b$save, so the link needs no runtime library ----
        .text
b$save:
     13 uj

// ---- bss ----
        .bss
b2:
        . = . + 2
