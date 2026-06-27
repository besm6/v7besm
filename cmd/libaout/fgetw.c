
#include <stdio.h>
#include "besm6/b.out.h"

// One full word == 48 bits == 6 bytes, read as two big-endian 24-bit
// half-words, high half-word first, so the six bytes form a plain big-endian
// 48-bit number.
uword_t fgetw(register FILE *f)
{
    register uword_t hi = (uword_t) fgeth(f);
    register uword_t lo = (uword_t) fgeth(f);

    return (hi << 24) | lo;
}
