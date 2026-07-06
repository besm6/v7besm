//
// BESM-6 architecture details.
//
#include "besm6_arch.h"

#include <iomanip>
#include <iostream>
#include <sstream>

//
// bit 48 -> 1, bit 47 -> 2 and so on.
// A one in bit 1 and the zero word -> 48,
// as in the original variant of the instruction set.
//
unsigned besm6_highest_bit(Word val)
{
    int n = 32, cnt = 0;
    do {
        Word tmp = val;
        if (tmp >>= n) {
            cnt += n;
            val = tmp;
        }
    } while (n >>= 1);
    return 48 - cnt;
}

//
// Pack a value by mask.
//
Word besm6_pack(Word val, Word mask)
{
    Word result;

    result = 0;
    for (; mask; mask >>= 1, val >>= 1)
        if (mask & 1) {
            result >>= 1;
            if (val & 1)
                result |= BIT48;
        }
    return result;
}

//
// Unpack a value by mask.
//
Word besm6_unpack(Word val, Word mask)
{
    Word result;
    unsigned i;

    result = 0;
    for (i = 0; i < 48; ++i) {
        result <<= 1;
        if (mask & BIT48) {
            if (val & BIT48)
                result |= 1;
            val <<= 1;
        }
        mask <<= 1;
    }
    return result;
}

//
// Count the number of one bits in a word.
//
unsigned besm6_count_ones(Word word)
{
    unsigned c;

    for (c = 0; word; ++c)
        word &= word - 1;
    return c;
}

//
// Check whether instruction is extracode.
//
bool is_extracode(unsigned opcode)
{
    switch (opcode) {
    case 050:
    case 051:
    case 052:
    case 053: // e50...e77
    case 054:
    case 055:
    case 056:
    case 057:
    case 060:
    case 061:
    case 062:
    case 063:
    case 064:
    case 065:
    case 066:
    case 067:
    case 070:
    case 071:
    case 072:
    case 073:
    case 074:
    case 075:
    case 076:
    case 077:
    case 0200: // e20
    case 0210: // e21
        return true;
    }
    return false;
}

static const char *opname_short_madlen[64] = {
    // clang-format off
    "atx",  "stx",  "mod",  "xts",  "a+x",  "a-x",  "x-a",  "amx",
    "xta",  "aax",  "aex",  "arx",  "avx",  "aox",  "a/x",  "a*x",
    "apx",  "aux",  "acx",  "anx",  "e+x",  "e-x",  "asx",  "xtr",
    "rte",  "yta",  "*32",  "ext",  "e+n",  "e-n",  "asn",  "ntr",
    "ati",  "sti",  "ita",  "its",  "mtj",  "j+m",  "*46",  "*47",
    "*50",  "*51",  "*52",  "*53",  "*54",  "*55",  "*56",  "*57",
    "*60",  "*61",  "*62",  "*63",  "*64",  "*65",  "*66",  "*67",
    "*70",  "*71",  "*72",  "*73",  "*74",  "*75",  "*76",  "*77",
    // clang-format on
};

static const char *opname_long_madlen[16] = {
    "*20", "*21", "utc", "wtc",  "vtm", "utm", "uza", "u1a",
    "uj",  "vjm", "ij",  "stop", "vzm", "v1m", "*36", "vlm",
};

//
// Return the mnemonic for an instruction opcode.
// The opcode must be in range 000..077 or 0200..0370.
//
const char *besm6_opname(unsigned opcode)
{
    // Madlen mnemonics.
    if (opcode & 0200)
        return opname_long_madlen[(opcode >> 3) & 017];
    return opname_short_madlen[opcode];
}

//
// Print a machine instruction with mnemonics.
//
void besm6_print_instruction_mnemonics(std::ostream &out, unsigned cmd)
{
    auto save_flags = out.flags();
    unsigned reg, opcode, addr;

    reg = (cmd >> 20) & 017;
    if (cmd & ONEBIT(20)) {
        opcode = (cmd >> 12) & 0370;
        addr   = cmd & BITS(15);
    } else {
        opcode = (cmd >> 12) & 077;
        addr   = cmd & 07777;
        if (cmd & ONEBIT(19))
            addr |= 070000;
    }
    out << besm6_opname(opcode) << std::oct;
    if (addr) {
        out << ' ';
        if (addr >= 077700)
            out << '-' << ((addr ^ 077777) + 1);
        else
            out << addr;
    }
    if (reg) {
        if (!addr)
            out << ' ';
        out << '(' << reg << ')';
    }

    // Restore.
    out.flags(save_flags);
}

//
// Print a machine instruction in octal.
//
void besm6_print_instruction_octal(std::ostream &out, unsigned cmd)
{
    auto save_flags = out.flags();

    out << std::oct << std::setfill('0') << std::setw(2) << (cmd >> 20) << ' ';
    if (cmd & ONEBIT(20)) {
        out << std::setfill('0') << std::setw(2) << ((cmd >> 15) & 037) << ' ';
        out << std::setfill('0') << std::setw(5) << (cmd & BITS(15));
    } else {
        out << std::setfill('0') << std::setw(3) << ((cmd >> 12) & 0177) << ' ';
        out << std::setfill('0') << std::setw(4) << (cmd & BITS(12));
    }

    // Restore.
    out.flags(save_flags);
}

//
// Print 48-bit value as octal.
//
void besm6_print_word_octal(std::ostream &out, Word value)
{
    auto save_flags = out.flags();

    out << std::oct;
    out << std::setfill('0') << std::setw(4) << ((int)(value >> 36) & BITS(12)) << ' ';
    out << std::setfill('0') << std::setw(4) << ((int)(value >> 24) & BITS(12)) << ' ';
    out << std::setfill('0') << std::setw(4) << ((int)(value >> 12) & BITS(12)) << ' ';
    out << std::setfill('0') << std::setw(4) << ((int)value & BITS(12));

    // Restore.
    out.flags(save_flags);
}

//
// Convert number to string as octal.
//
std::string to_octal(unsigned val)
{
    std::ostringstream buf;
    buf << std::oct << val;
    return buf.str();
}
