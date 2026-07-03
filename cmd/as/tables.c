//
// Assembler for BESM-6.
// Read-only conversion and instruction tables.
//
#include <stdio.h>

#include "as.h"

// Character classification table, indexed by a byte value.  Each entry is a
// bit set tested by the IS* macros in as.h:
//   bit 0 (1) = hex digit      0-9 A-F a-f
//   bit 1 (2) = octal digit    0-7
//   bit 2 (4) = decimal digit  0-9
//   bit 3 (8) = "letter"       valid in an identifier
// '.' and '_' are flagged as letters so they may appear in names; while a
// machine instruction is expected the lexer also treats '+ - * /' as letters
// so mnemonics like "a+x" scan as a single name (see read_name()).  So e.g.
// '0'..'7' carry 1|2|4 = 7, '8'/'9' carry 1|4 = 5, and 'A'..'F'/'a'..'f' carry
// 1|8 = 9.
const int ctype[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 7, 7, 7, 7, 7, 7, 7, 7, 5, 5, 0, 0, 0, 0, 0, 0,
    0, 9, 9, 9, 9, 9, 9, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 0, 0, 0, 0, 8,
    0, 9, 9, 9, 9, 9, 9, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 0,
};

const int segmtype[] = {
    // convert segment number to symbol type.  Indexed by the S* segment numbers,
    // which are NOT contiguous: SEXT==6 and SABS==7 leave slot 5 reserved
    // (formerly abss), so it must be present here or SEGMTYPE(SEXT)/SEGMTYPE(SABS)
    // read the wrong (or out-of-bounds) entry.
    N_CONST, // SCONST 0
    N_TEXT,  // STEXT  1
    N_DATA,  // SDATA  2
    N_STRNG, // SSTRNG 3
    N_BSS,   // SBSS   4
    0,       // slot 5 - reserved (formerly abss)
    N_UNDF,  // SEXT   6
    N_ABS,   // SABS   7
};

const int segmrel[] = {
    // convert segment number to relocation type (same non-contiguous indexing as
    // segmtype: slot 5 is reserved, formerly abss).
    RCONST, // SCONST 0
    RTEXT,  // STEXT  1
    RDATA,  // SDATA  2
    RSTRNG, // SSTRNG 3
    RBSS,   // SBSS   4
    0,      // slot 5 - reserved (formerly abss)
    REXT,   // SEXT   6
    RABS,   // SABS   7
};

const int typesegm[] = {
    // convert symbol type to segment number.  Indexed by the N_* type numbers,
    // which are NOT contiguous: type 06 is reserved (formerly N_ABSS) and
    // N_STRNG==07, so the reserved slot must be present here or TYPESEGM(N_STRNG)
    // reads out of bounds.
    SEXT,   // N_UNDF  0
    SABS,   // N_ABS   1
    SCONST, // N_CONST 2
    STEXT,  // N_TEXT  3
    SDATA,  // N_DATA  4
    SBSS,   // N_BSS   5
    0,      // type 6  - reserved (formerly abss)
    SSTRNG, // N_STRNG 7
};

// Table of machine instructions.
//
// BESM-6 opcodes occupy the same bit positions as in the hardware instruction
// word (octal, bit 1 = LSB); pass1.c ORs the modifier (index << 20)
// and the address field into `val`.
//
//   short-address (opcodes 000-077):  val = opcode << 12  -> 0zz0000
//   long-address  (opcodes 020-037):  val = opcode << 15  -> 0zz00000
//
const struct table table[] = {
    // Short-address instructions (Format 1, opcodes 000-077).  Opcodes that
    // have no mnemonic (e.g. 032, 033, 046, 047 and 050-077) are reached via
    // the raw "$NN" form instead and so are absent here.  The Cyrillic name in
    // parentheses is the original BESM-6 mnemonic; see doc/Besm6_Instruction_Set.md.
    { 00000000L, "atx", 0 }, // store accumulator to memory (зп)
    { 00010000L, "stx", 0 }, // store accumulator and pop the stack (зпм)
    { 00020000L, "mod", 0 }, // modify privileged registers (рег)
    { 00030000L, "xts", 0 }, // push the stack, then load (счм)
    { 00040000L, "a+x", 0 }, // add (сл)
    { 00050000L, "a-x", 0 }, // subtract (вч)
    { 00060000L, "x-a", 0 }, // reverse subtract: memory - accumulator (вчоб)
    { 00070000L, "amx", 0 }, // subtract absolute values (вчаб)
    { 00100000L, "xta", 0 }, // load memory into the accumulator (сч)
    { 00110000L, "aax", 0 }, // bitwise AND (и)
    { 00120000L, "aex", 0 }, // bitwise XOR (нтж)
    { 00130000L, "arx", 0 }, // cyclical add with end-around carry (слц)
    { 00140000L, "avx", 0 }, // conditionally negate by sign of memory (знак)
    { 00150000L, "aox", 0 }, // bitwise OR (или)
    { 00160000L, "a/x", 0 }, // divide (дел)
    { 00170000L, "a*x", 0 }, // multiply (умн)
    { 00200000L, "apx", 0 }, // pack bits under a mask (сбр)
    { 00210000L, "aux", 0 }, // unpack bits under a mask (рзб)
    { 00220000L, "acx", 0 }, // population count (чед)
    { 00230000L, "anx", 0 }, // find the highest set bit (нед)
    { 00240000L, "e+x", 0 }, // add exponent from memory (слп)
    { 00250000L, "e-x", 0 }, // subtract exponent from memory (вчп)
    { 00260000L, "asx", 0 }, // arithmetic shift by exponent in memory (сд)
    { 00270000L, "xtr", 0 }, // set the mode register from memory (рж)
    { 00300000L, "rte", 0 }, // read the mode register into the exponent (счрж)
    { 00310000L, "yta", 0 }, // get the younger-bits (Y) register (счмр)
    // $32 - full-width I/O read, no mnemonic
    // $33 - full-width I/O read, no mnemonic
    { 00340000L, "e+n", 0 }, // add immediate to exponent (слпа)
    { 00350000L, "e-n", 0 }, // subtract immediate from exponent (вчпа)
    { 00360000L, "asn", 0 }, // arithmetic shift by immediate (сда)
    { 00370000L, "ntr", 0 }, // set the mode register from immediate (ржа)
    { 00400000L, "ati", 0 }, // copy accumulator to an index register (уи)
    { 00410000L, "sti", 0 }, // store to an index register and pop (уим)
    { 00420000L, "ita", 0 }, // copy an index register to the accumulator (счи)
    { 00430000L, "its", 0 }, // push accumulator, then load an index register (счим)
    { 00440000L, "mtj", 0 }, // copy one index register to another (уии)
    { 00450000L, "j+m", 0 }, // add two index registers (сли)
    // $46 - special memory access, no mnemonic
    // $47 - reserved, no mnemonic
    // $50...$77 - extracodes, reached via "$NN"

    // Long-address instructions (Format 2, opcodes 020-037), with a 15-bit
    // address.  Opcodes 020/021 and 036 have no mnemonic (use "@NN").
    // @20
    // @21
    { 02200000L, "utc", TLONG },           // set the C register from an immediate address (мода)
    { 02300000L, "wtc", TLONG },           // set the C register from memory (мод)
    { 02400000L, "vtm", TLONG },           // set an index register to an immediate (уиа)
    { 02500000L, "utm", TLONG },           // add an immediate to an index register (слиа)
    { 02600000L, "uza", TLONG },           // branch if the condition flag ω = 0 (по)
    { 02700000L, "u1a", TLONG },           // branch if the condition flag ω != 0 (пе)
    { 03000000L, "uj", TLONG },            // unconditional branch (пб)
    { 03100000L, "vjm", TLONG | TALIGN },  // jump to subroutine, saving the return address (пв)
    { 03200000L, "ij", TLONG | TALIGN },   // return from interrupt (выпр)
    { 03300000L, "stop", TLONG | TALIGN }, // stop the processor (стоп)
    { 03400000L, "vzm", TLONG },           // branch if an index register is zero (пио)
    { 03500000L, "v1m", TLONG },           // branch if an index register is not zero (пино)
    // @36 - undocumented, behaves like vzm
    { 03700000L, "vlm", TLONG }, // loop: bump an index register and branch while nonzero (цикл)

    { 0, 0L, 0 }, // sentinel: a NULL name ends the table
};
