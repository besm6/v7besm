// The host-vs-native disassembler agreement fixture (task C9c).
//
// disasm walks const, text and data in on-disk order, so all three are here and
// none is empty.  The text is chosen to reach every branch of disasm_insn():
//
//   * BOTH INSTRUCTION FORMATS -- long-address (opcodes 020-037, a 15-bit
//     address) and short-address (000-077, 12 bits, with the segment bit that
//     extends it to 15).
//   * BOTH OPERAND PRINTINGS -- an address below 8 prints bare, one above it
//     with a leading zero (%#o), and a zero address prints as nothing at all.
//   * AN INDEX REGISTER AND NONE, since the register column is printed either
//     way and its absence is three spaces.
//
// NOTHING IS UNDEFINED HERE: the same fixture is linked into an image for the
// second half of the suite, and an unresolved external would fail the link
// rather than the comparison.  Everything is likewise WORD-ALIGNED in pairs --
// the disassembler prints two half-words per line, and an odd tail would make
// the two runs differ in padding rather than in decoding, which is not what
// this asserts.

        .globl  entry

// ---- const ----
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
        xta     ctable                  // short format, a const reference
        a+x     cdatum
        atx     d1                      // ... and a data one
        xta     #0777000777000          // an anonymous literal
     13 xta     ctable                  // the same, indexed: the register column
     14 atx     d2
        uj      done                    // long format, a text address
        vtm     7,3                     // a long-address operand below 8: bare
        utm     0100,5                  // ... and above it: %#o
        xta                             // address 0: the operand is omitted
done:
        stop    012

// ---- data ----
        .data
d1:
        .word   0
d2:
        .word   1
        .word   entry, cdatum           // a text address and a const one
