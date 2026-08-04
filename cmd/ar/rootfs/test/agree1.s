// Member one of the host-vs-native ar agreement fixture (task C9d).
//
// ar copies member BYTES and writes member HEADERS, so what a fixture has to
// give it is a set of files whose lengths land differently against the six-byte
// word: copy_member() pads the data up to a word boundary and records the padded
// size in the header, and a set of members that all happened to be a whole
// number of words would never exercise the pad at all.
//
// The four members are deliberately different lengths, and none of them is a
// multiple of six bytes by accident -- see agree4.s, which is the odd one built
// to straddle the 512-byte copy chunk.
//
// They are real objects rather than arbitrary bytes because ranlib's fixture is
// the same set: it needs a symbol table in every member, and one global apiece
// with no name repeated anywhere is what keeps the __.SYMDEF comparison honest.

        .globl  one_text, one_data

// ---- const: 2 words ----
        .const
one_const:
        .word   0x111111111111
        .word   0x222222222222

// ---- text: 3 words ----
        .text
one_text:
        xta     one_const
        atx     one_data
        stop    012

// ---- data: 1 word ----
        .data
one_data:
        .word   one_text

// ---- bss: 2 words ----
        .bss
one_bss:
        . = . + 2
