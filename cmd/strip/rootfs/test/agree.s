// The host-vs-native strip agreement fixture (task C9c).
//
// strip drops the symbol table and the relocation records and keeps everything
// before them, so what the fixture needs is a FAT symbol table over a body big
// enough to make the copy loop go round more than once.  ../strip.c copies in
// BUFSZ-byte chunks and BUFSZ is the one number the BESM-6 build changes -- 3,072
// bytes there against 8,192 here -- so a body of a few hundred words takes a
// different number of iterations in the two builds and the result must still be
// identical byte for byte.  That is the whole point of the case.
//
// The const block below is 600 words, i.e. 3,600 bytes: two chunks on the target
// and one on the host, which is exactly the disagreement that has to come out
// even.

        .globl  entry, gdata, gbss

// ---- const: 600 words, deliberately more than one target-side chunk ----
        .const
ctable:
        . = . + 600
cend:

// ---- text ----
        .text
entry:
        xta     ctable
        atx     gdata
        uj      done
done:
        stop    012

// ---- data ----
        .data
gdata:
        .word   0
ldata:
        .word   entry, ctable

// ---- bss ----
        .bss
gbss:
        . = . + 4
