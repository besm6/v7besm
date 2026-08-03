// Part one of the host-vs-native linker agreement fixture (task C9b).
// Paired with link2.s, which defines what this references and vice versa, so
// the link exercises resolution in both directions.
//
// The "#expr" literals below are deliberately DUPLICATED in link2.s: an
// anonymous literal carries RMERGE, and merging two of them across object files
// is the whole subject of load_constants() and of the newindex[] map that
// repoints every const reference afterwards.  If the two builds disagreed about
// which word survived, the images would differ here first.

        .globl  entry, shared1, cdatum
        .comm   commblk, 4

// ---- const: named words, and a label into the middle of them ----
        .const
ctable:
        .word   0x111111111111
        .word   0x222222222222
cdatum:
        .word   0x333333333333
cend:
        .word   ctable                  // a const word holding a const address

// ---- text ----
        .text
entry:
        its     13
     13 vjm     b$save
        xta     #0777000777000          // a literal link2.s also asks for
        atx     d1
        xta     #0123456                // ... and another
        a+x     #1
        xta     ctable                  // a direct const reference
        xta     cdatum
        atx     [cend]                  // the wtc escape
        xta     commblk
     13 vjm     shared2                 // an external call, resolved by link2.s
        xta     shared3                 // an external datum, ditto
        atx     d2
        uj      done
done:
        stop    012

// ---- data ----
        .data
d1:
        .word   0
d2:
        .word   0
d3:
        .word   entry, cdatum, shared2  // text, const and external addresses

// ---- bss ----
        .bss
b1:
        . = . + 3

// ---- an exported datum link2.s reads ----
        .data
shared1:
        .word   0x0f0f0f0f0f0f
