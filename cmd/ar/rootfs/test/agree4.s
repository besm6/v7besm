// Member four of the ar agreement fixture (task C9d), and the only one whose
// size matters.
//
// copy_member() moves a member's bytes in 512-byte chunks out of ar.iobuf, and
// 512 is not a multiple of the six-byte word: every chunk boundary after the
// first lands in the MIDDLE of a word, and the trailing pad is computed from the
// member's total length rather than from what is left in the last chunk.  A
// member that fits in one chunk exercises none of that.
//
// The const block below is 200 words, i.e. 1,200 bytes: three chunks, two of
// them full, with 176 bytes in the last.  This is also the member the -r and -m
// cases move about, so the header rewritten at a new offset is a long one.

        .globl  four_text, four_data

// ---- const: 200 words, i.e. more than two 512-byte copy chunks ----
        .const
four_const:
        . = . + 200
four_cend:

// ---- text ----
        .text
four_text:
        xta     four_const
        atx     four_data
        stop    012

// ---- data ----
        .data
four_data:
        .word   0
        .word   four_text
        .word   four_const

// ---- bss ----
        .bss
four_bss:
        . = . + 3
