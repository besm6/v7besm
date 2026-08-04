// Member two of the ar agreement fixture (task C9d) -- a different length from
// member one, and a different symbol set, so that a table of contents and a
// __.SYMDEF over the pair have something to get wrong.

        .globl  two_text, two_bss

// ---- const: 1 word ----
        .const
two_const:
        .word   0x333333333333

// ---- text: 5 words ----
        .text
two_text:
        xta     two_const
        a+x     two_const
        atx     two_data
        xta     two_data
        stop    012

// ---- data: 3 words ----
        .data
two_data:
        .word   0
        .word   two_text
        .word   two_const

// ---- bss: 4 words ----
        .bss
two_bss:
        . = . + 4
