//
// Assembler for BESM-6.
// Read-only conversion and instruction tables.
//
#include <stdio.h>

#include "as.h"

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
    // which are NOT contiguous: SEXT==6 and SABS==7 leave slot 5 (abss) unused,
    // so it must be present here or SEGMTYPE(SEXT)/SEGMTYPE(SABS) read the wrong
    // (or out-of-bounds) entry.
    N_CONST, // SCONST 0
    N_TEXT,  // STEXT  1
    N_DATA,  // SDATA  2
    N_STRNG, // SSTRNG 3
    N_BSS,   // SBSS   4
    N_ABSS,  // (abss) 5 - unused placeholder
    N_UNDF,  // SEXT   6
    N_ABS,   // SABS   7
};

const int segmrel[] = {
    // convert segment number to relocation type (same non-contiguous indexing as
    // segmtype: slot 5 is the unused abss placeholder).
    RCONST, // SCONST 0
    RTEXT,  // STEXT  1
    RDATA,  // SDATA  2
    RSTRNG, // SSTRNG 3
    RBSS,   // SBSS   4
    RABSS,  // (abss) 5 - unused placeholder
    REXT,   // SEXT   6
    RABS,   // SABS   7
};

const int typesegm[] = {
    // convert symbol type to segment number.  Indexed by the N_* type numbers,
    // which are NOT contiguous: N_ABSS==06 and N_STRNG==07, so the abss slot must
    // be present here or TYPESEGM(N_STRNG) reads out of bounds and TYPESEGM(N_ABSS)
    // returns the wrong segment.
    SEXT,   // N_UNDF  0
    SABS,   // N_ABS   1
    SCONST, // N_CONST 2
    STEXT,  // N_TEXT  3
    SDATA,  // N_DATA  4
    SBSS,   // N_BSS   5
    5,      // N_ABSS  6 - abss segment, unused placeholder
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
    // Short-address instructions (Format 1, opcodes 000-077).
    { 00000000L, "atx", 0 },
    { 00010000L, "stx", 0 },
    { 00020000L, "mod", 0 },
    { 00030000L, "xts", 0 },
    { 00040000L, "a+x", 0 },
    { 00050000L, "a-x", 0 },
    { 00060000L, "x-a", 0 },
    { 00070000L, "amx", 0 },
    { 00100000L, "xta", 0 },
    { 00110000L, "aax", 0 },
    { 00120000L, "aex", 0 },
    { 00130000L, "arx", 0 },
    { 00140000L, "avx", 0 },
    { 00150000L, "aox", 0 },
    { 00160000L, "a/x", 0 },
    { 00170000L, "a*x", 0 },
    { 00200000L, "apx", 0 },
    { 00210000L, "aux", 0 },
    { 00220000L, "acx", 0 },
    { 00230000L, "anx", 0 },
    { 00240000L, "e+x", 0 },
    { 00250000L, "e-x", 0 },
    { 00260000L, "asx", 0 },
    { 00270000L, "xtr", 0 },
    { 00300000L, "rte", 0 },
    { 00310000L, "yta", 0 },
    // $32
    // $33
    { 00340000L, "e+n", 0 },
    { 00350000L, "e-n", 0 },
    { 00360000L, "asn", 0 },
    { 00370000L, "ntr", 0 },
    { 00400000L, "ati", 0 },
    { 00410000L, "sti", 0 },
    { 00420000L, "ita", 0 },
    { 00430000L, "its", 0 },
    { 00440000L, "mtj", 0 },
    { 00450000L, "j+m", 0 },
    // $46
    // $47
    // $50...$77

    // Long-address instructions (Format 2, opcodes 020-037).
    // @20
    // @21
    { 02200000L, "utc", TLONG },
    { 02300000L, "wtc", TLONG },
    { 02400000L, "vtm", TLONG },
    { 02500000L, "utm", TLONG },
    { 02600000L, "uza", TLONG },
    { 02700000L, "u1a", TLONG },
    { 03000000L, "uj", TLONG },
    { 03100000L, "vjm", TLONG | TALIGN },
    { 03200000L, "ij", TLONG | TALIGN },
    { 03300000L, "stop", TLONG | TALIGN },
    { 03400000L, "vzm", TLONG },
    { 03500000L, "v1m", TLONG },
    // @36
    { 03700000L, "vlm", TLONG },

    { 0, 0L, 0 },
};
