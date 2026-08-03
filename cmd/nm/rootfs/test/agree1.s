// Part one of the host-vs-native nm agreement fixture (task C9c).
//
// nm's whole output is one line per symbol -- value, class letter, name -- so the
// fixture's job is to put a symbol of EVERY CLASS in front of it, since the class
// is a switch in nm() with a branch per N_TYPE and the letter is upper-cased for
// an external.  Here: const (l/L), text (t/T), data (d/D), bss (b/B), absolute
// (a/A), common (c/C) and undefined (u/U), each in both cases where both exist.
//
// THE VALUES ARE CHOSEN TO SORT DIFFERENTLY FROM THE NAMES, which is what makes
// -n a real assertion rather than a re-run of the default: sorted by name the
// order is alpha..., sorted by value it is not.  Paired with agree2.s, which is
// the archive's second member and defines what this leaves undefined.

        .globl  gtext, gdata, gbss, gconst, gabs
        .comm   commblk, 4
        .comm   commtwo, 2

// An absolute symbol: no segment, so nm prints `a' -- and its value is the
// largest here, to put it last under -n and first under -n -r.
gabs    =       07777

// ---- const ----
        .const
gconst:
        .word   0x111111111111
lconst:                                 // a LOCAL const: lower-case `l'
        .word   0x222222222222

// ---- text ----
        .text
gtext:
        xta     gconst
     13 vjm     zsecond                 // undefined here: agree2.s has it
        xta     zdatum                  // ... and so is this one
        atx     gdata
ltext:                                  // a local text label: lower-case `t'
        uj      gtext
        stop    012

// ---- data ----
        .data
gdata:
        .word   0
ldata:
        .word   gtext, gconst

// ---- bss ----
        .bss
gbss:
        . = . + 3
lbss:
        . = . + 2
