//
// Trivial assembler.
//
#include <cstring>
#include <string>

#include "besm6_arch.h"

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
// Выдача мнемоники по коду инструкции.
// Код должен быть в диапазоне 000..077 или 0200..0370.
//
const char *besm6_opname(unsigned opcode)
{
    // Madlen mnemonics.
    if (opcode & 0200)
        return opname_long_madlen[(opcode >> 3) & 017];
    return opname_short_madlen[opcode];
}
