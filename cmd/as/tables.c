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
    // convert segment number to symbol type
    N_CONST, // SCONST
    N_TEXT,  // STEXT
    N_DATA,  // SDATA
    N_STRNG, // SSTRNG
    N_BSS,   // SBSS
    N_UNDF,  // SEXT
    N_ABS,   // SABS
};

const int segmrel[] = {
    // convert segment number to relocation type
    RCONST, // SCONST
    RTEXT,  // STEXT
    RDATA,  // SDATA
    RSTRNG, // SSTRNG
    RBSS,   // SBSS
    REXT,   // SEXT
    RABS,   // SABS
};

const int typesegm[] = {
    // convert symbol type to segment number
    SEXT,   // N_UNDF
    SABS,   // N_ABS
    SCONST, // N_CONST
    STEXT,  // N_TEXT
    SDATA,  // N_DATA
    SBSS,   // N_BSS
    SSTRNG, // N_STRNG
};

// Table of machine instructions.
//
// BESM-6 opcodes occupy the same bit positions as in the hardware instruction
// word (octal, bit 1 = LSB); pass1.c ORs the modifier (index << 28)
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
    { 00320000L, "e32", 0 },
    { 00330000L, "e33", 0 },
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
    { 00460000L, "e46", 0 },
    { 00470000L, "e47", 0 },
    { 00500000L, "e50", 0 },
    { 00510000L, "e51", 0 },
    { 00520000L, "e52", 0 },
    { 00530000L, "e53", 0 },
    { 00540000L, "e54", 0 },
    { 00550000L, "e55", 0 },
    { 00560000L, "e56", 0 },
    { 00570000L, "e57", 0 },
    { 00600000L, "e60", 0 },
    { 00610000L, "e61", 0 },
    { 00620000L, "e62", 0 },
    { 00630000L, "e63", 0 },
    { 00640000L, "e64", 0 },
    { 00650000L, "e65", 0 },
    { 00660000L, "e66", 0 },
    { 00670000L, "e67", 0 },
    { 00700000L, "e70", 0 },
    { 00710000L, "e71", 0 },
    { 00720000L, "e72", 0 },
    { 00730000L, "e73", 0 },
    { 00740000L, "e74", 0 },
    { 00750000L, "e75", 0 },
    { 00760000L, "e76", 0 },
    { 00770000L, "e77", 0 },

    // Long-address instructions (Format 2, opcodes 020-037).
    { 02000000L, "e20", TLONG },
    { 02100000L, "e21", TLONG },
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
    { 03600000L, "e36", TLONG },
    { 03700000L, "vlm", TLONG },

    { 0, 0L, 0 },
};
