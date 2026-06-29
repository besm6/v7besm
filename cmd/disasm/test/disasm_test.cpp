//
// Unit tests for the cmd/disasm disassembler engine.
//
// The engine is driven through disasm_insn(insn, buf), which decodes one 24-bit
// BESM-6 instruction into re-assemblable text.  This mirrors cmd/as's
// Assemble.AllInstructions: it feeds the very same encodings that the assembler
// emits and checks the text the disassembler produces for each one.
//
#include <gtest/gtest.h>

#include <string>

// The engine library is compiled as C; disasm.h is C++-safe.
extern "C" {
#include "disasm.h"
}

// Decode one 24-bit instruction into a string.
static std::string decode(unsigned insn)
{
    char buf[64];
    disasm_insn(insn, buf);
    return std::string(buf);
}

// Decode every machine instruction from the same encodings that
// Assemble.AllInstructions emits.  A text word packs two 24-bit instructions;
// short-address opcodes are val = opcode << 12 with a 12-bit address, long
// opcodes are val = opcode << 15 (bit 19 = long-format flag) with a 15-bit
// address.  An index/modifier register sits in bits 20-23 (reg << 20).  The
// expected text uses the MADLEN dialect (the default), whose names match the
// assembler's table[] in cmd/as/tables.c; opcodes the assembler does not name
// disassemble to the raw "$NN" (short) / "@NN" (long) forms it accepts back.
TEST(Disassemble, AllInstructions)
{
    // Short-address instructions (opcodes 000-077).
    EXPECT_EQ(decode(00000000 | 0123), "atx 0123");
    EXPECT_EQ(decode(00010000 | 0144), "stx 0144");
    EXPECT_EQ(decode(00020000 | 052), "mod 052");
    EXPECT_EQ(decode(00030000 | 020), "xts 020");
    EXPECT_EQ(decode(00040000 | 0123), "a+x 0123");
    EXPECT_EQ(decode(00050000 | 0123), "a-x 0123");
    EXPECT_EQ(decode(00060000 | 0307), "x-a 0307");
    EXPECT_EQ(decode(00070000 | 0123), "amx 0123");
    EXPECT_EQ(decode(00100000 | 011), "xta 011"); // address == word 9
    EXPECT_EQ(decode(00110000 | 0123), "aax 0123");
    EXPECT_EQ(decode((5u << 20) | 00120000 | 0123), "aex 0123, 5"); // indexed
    EXPECT_EQ(decode(00130000 | 07654), "arx 07654");
    EXPECT_EQ(decode(00140000 | 0100), "avx 0100");
    EXPECT_EQ(decode(00150000 | 0100), "aox 0100");
    EXPECT_EQ(decode(00160000 | 077), "a/x 077");
    EXPECT_EQ(decode(00170000 | 012), "a*x 012");
    EXPECT_EQ(decode(00200000 | 1), "apx 1"); // 1-7: no leading 0
    EXPECT_EQ(decode(00210000 | 011), "aux 011");
    EXPECT_EQ(decode(00220000 | 7), "acx 7");
    EXPECT_EQ(decode(00230000 | 0123), "anx 0123");
    EXPECT_EQ(decode(00240000 | 010), "e+x 010");
    EXPECT_EQ(decode(00250000 | 0234), "e-x 0234");
    EXPECT_EQ(decode(00260000 | 07777), "asx 07777");
    EXPECT_EQ(decode((4u << 20) | 00270000 | 0567), "xtr 0567, 4"); // leading modreg
    EXPECT_EQ(decode(00300000 | 077), "rte 077");
    EXPECT_EQ(decode(00310000 | 1), "yta 1");
    EXPECT_EQ(decode(00320000 | 0123), "$32 0123"); // raw short opcode
    EXPECT_EQ(decode(00330000 | 0246), "$33 0246"); // raw short opcode
    EXPECT_EQ(decode(00340000 | 010), "e+n 010");
    EXPECT_EQ(decode(00350000 | 020), "e-n 020");
    EXPECT_EQ(decode(00360000 | 0300), "asn 0300");
    EXPECT_EQ(decode(00370000 | 0252), "ntr 0252");
    EXPECT_EQ(decode(00400000 | 2), "ati 2");
    EXPECT_EQ(decode(00410000 | 3), "sti 3");
    EXPECT_EQ(decode(00420000 | 4), "ita 4");
    EXPECT_EQ(decode(00430000 | 5), "its 5");
    EXPECT_EQ(decode((1u << 20) | 00440000 | 6), "mtj 6, 1"); // indexed
    EXPECT_EQ(decode((8u << 20) | 00450000 | 7), "j+m 7, 8"); // leading modreg
    EXPECT_EQ(decode(00460000 | 050), "$46 050");
    EXPECT_EQ(decode(00470000 | 051), "$47 051");
    EXPECT_EQ(decode(00500000 | 052), "$50 052");
    EXPECT_EQ(decode(00510000 | 053), "$51 053");
    EXPECT_EQ(decode(00520000 | 054), "$52 054");
    EXPECT_EQ(decode(00530000 | 055), "$53 055");
    EXPECT_EQ(decode(00540000 | 056), "$54 056");
    EXPECT_EQ(decode(00550000 | 057), "$55 057");
    EXPECT_EQ(decode(00560000 | 060), "$56 060");
    EXPECT_EQ(decode(00570000 | 061), "$57 061");
    EXPECT_EQ(decode(00600000 | 062), "$60 062");
    EXPECT_EQ(decode(00610000 | 063), "$61 063");
    EXPECT_EQ(decode(00620000 | 064), "$62 064");
    EXPECT_EQ(decode(00630000 | 065), "$63 065");
    EXPECT_EQ(decode(00640000 | 066), "$64 066");
    EXPECT_EQ(decode(00650000 | 067), "$65 067");
    EXPECT_EQ(decode(00660000 | 070), "$66 070");
    EXPECT_EQ(decode(00670000 | 071), "$67 071");
    EXPECT_EQ(decode(00700000 | 072), "$70 072");
    EXPECT_EQ(decode(00710000 | 073), "$71 073");
    EXPECT_EQ(decode(00720000 | 074), "$72 074");
    EXPECT_EQ(decode(00730000 | 075), "$73 075");
    EXPECT_EQ(decode(00740000 | 076), "$74 076");
    EXPECT_EQ(decode(00750000 | 077), "$75 077");
    EXPECT_EQ(decode(00760000 | 0100), "$76 0100");
    EXPECT_EQ(decode(00770000 | 0101), "$77 0101");

    // Long-address instructions (opcodes 020-037, bit 19 set, 15-bit address).
    EXPECT_EQ(decode(02000000 | 040000), "@20 040000");
    EXPECT_EQ(decode(02100000 | 041234), "@21 041234");
    EXPECT_EQ(decode(02200000 | 050000), "utc 050000");
    EXPECT_EQ(decode(02300000 | 0123), "wtc 0123");
    EXPECT_EQ(decode((2u << 20) | 02400000 | 040000), "vtm 040000, 2"); // indexed
    EXPECT_EQ(decode(02500000 | 010), "utm 010");
    EXPECT_EQ(decode(02600000 | 011), "uza 011"); // address == word 9
    EXPECT_EQ(decode(02700000 | 031), "u1a 031"); // address == word 25
    EXPECT_EQ(decode(03000000 | 0), "uj");        // zero address: mnemonic only
    EXPECT_EQ(decode(03100000 | 060000), "vjm 060000");
    EXPECT_EQ(decode(03200000 | 0123), "ij 0123");
    EXPECT_EQ(decode(03300000 | 0456), "stop 0456");
    EXPECT_EQ(decode(03400000 | 070000), "vzm 070000");
    EXPECT_EQ(decode(03500000 | 077777), "v1m 077777"); // max 15-bit
    EXPECT_EQ(decode(03600000 | 012345), "@36 012345");
    EXPECT_EQ(decode(03700000 | 031), "vlm 031"); // address == word 25

    // The utc filler the assembler inserts to keep instructions word-aligned.
    EXPECT_EQ(decode(02200000), "utc");

    // Short-address extension bit (op_scmd & 0100): address gains 070000.
    EXPECT_EQ(decode(01000000 | 0123), "atx 070123");
}

// The -b flag switches lcmd/scmd to the Cyrillic BEMSH dialect.
TEST(Disassemble, BemshDialect)
{
    scmd = scmd_bemsh;
    lcmd = lcmd_bemsh;
    EXPECT_EQ(decode(00000000 | 0123), "зп 0123");  // atx -> зп
    EXPECT_EQ(decode(02200000 | 050000), "мода 050000"); // utc -> мода
    EXPECT_EQ(decode(03300000 | 0456), "стоп 0456"); // stop -> стоп

    // Restore the default MADLEN dialect for any later tests.
    scmd = scmd_madlen;
    lcmd = lcmd_madlen;
}
