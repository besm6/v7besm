
#include <stdio.h>
#include "besm6/b.out.h"

// One full word == 48 bits == 6 bytes, read as two little-endian 24-bit
// half-words, low half-word first.
uword_t fgetw(register FILE *f)
{
    register uword_t lo = (uword_t) fgeth(f);
    register uword_t hi = (uword_t) fgeth(f);

    return lo | (hi << 24);
}
