// The assembler's own surface, for the host-vs-native agreement test (task C9b).
// Everything here is deliberately hand-written rather than compiler output:
// compiled.s covers what b6cc actually emits, and this covers what the ASSEMBLER
// can be asked for, which is a strictly larger language.  Between them the two
// fixtures reach every path in lex.c and expr.c that a real object can depend on.
//
// Nothing here may depend on a "# N \"file\"" marker: run-as-test.sh copies the
// fixture in and names it relatively so the recorded path is stable, and a marker
// would pin an absolute one.  See doc/Assembler_Manual.md for the language.

        .globl  entry, extfn, extdat
        .comm   commblk, 6

// ---- const: literals in every base, and words placed by .const ----
        .const
cwords:
        .word   0, 1, -1
        .word   0777777777777777777     // every bit set, in octal
        .word   0x0123456789ab          // hexadecimal, full 48 bits
        .word   0b101010101010          // binary
        .word   1'000'000               // decimal with C++ digit separators
        .word   0x'dead'beef            // separators inside a left-aligned literal
        .word   0'123                   // left-aligned octal
        .word   0x'abc                  // left-aligned hexadecimal
        .word   0b'111                  // left-aligned binary
        .word   .[1:24]                 // bit-mask literal, low half
        .word   .[25:48]                // ... high half
        .word   .[1:48]                 // ... the whole word
        .word   .[48=41]                // ... complement form: clears the exponent
        .word   .1, .24, .25, .48       // single-bit literals on both sides of 24
chalves:
        .half   0, 077777777, 1234, 0   // two per word, high half first

// ---- expressions: every operator, both precedence rules ----
eq1     = 6 * 7
eq2     = (1 << 12) - 1
eq3     = 0777 & 0170 | 07 ^ 03
eq4     = 100 / 7 % 5
eq5     = -eq1 + ~eq2
eq6     = 0777777 ~ 0700000              // XOR with the complement
eq7     = ((((1 + 2) * 3) - 4) / 5)      // nesting, well inside MAXEXPRDEPTH
eq8     = {0777777777777777777}          // truncate the exponent field
eq9     = 0140000000000000000 >> 24
eq10    = 1 + 2 << 3                     // shifts bind looser than '+': (1+2)<<3
        .equ    eq11, cwords + 2         // the .equ spelling, and a relocatable
        .word   eq1, eq2, eq3, eq4, eq5
        .word   eq6, eq7, eq8, eq9, eq10
        .word   eq11
        .org    0400                     // absolute origin; .const only
origin:
        .word   0

// ---- text: instruction encodings and every operand form ----
        .text
entry:
        its     13
     13 vjm     b$save                   // a modifier register written on the left
     15 utm     -8
        xta     cwords                   // direct address into const
        atx     dwords                   // ... into data
      6 xta     3
      7 atx
        xts     #0123456                 // an interned "#expr" literal
        xta     #eq1                     // ... naming an .equ
        xta     #cwords                  // ... a relocatable, always pooled
        xta     #0                       // ... absolute zero, folded to word 0
        a+x     #1
        a-x     #1
        aax     chalves, 3               // the trailing index form
        aox     chalves, 0               // an explicit M0 is allowed
        a*x     cwords
        a/x     cwords
        x-a     cwords
        ntr     7
        ati     5
        ita     5
        mtj     3
        j+m     4
        uza     local
        u1a     local
local:
        xta     extdat                   // an external data reference
     13 vjm     extfn                    // an external call
        xta     commblk                  // a common block
        xta     <cwords>                 // the utc escape, for an unreachable address
        atx     [cwords]                 // ... and the wtc form
        vtm     -10, 2
loop:   xta     cwords, 2
        vlm     loop, 2
        @20     0                        // a raw long opcode
        $77     5                        // ... and a raw short one: the syscall extracode
        stop    012
        uj      entry

// ---- data and bss ----
        .data
dwords:
        .word   0, eq1, cwords, entry    // absolute, const-relative, text-relative
dext:
        .word   extdat
        .half   1, 2, 3, 4

        .bss
bwords:
        . = . + 4
bmore:
        . = . + 1

// ---- strings ----
        .strng
smsg:
        .ascii  "hello, world\n\0"
sesc:
        .ascii  "\t\r\b\f\\\"\101\0"     // named escapes, an octal one, and a quote
