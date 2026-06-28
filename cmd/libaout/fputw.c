
#include <stdio.h>

#include "besm6/b.out.h"

// One full word == 48 bits == 6 bytes, stored as two big-endian 24-bit
// half-words, high half-word first, so the six bytes form a plain big-endian
// 48-bit number.
void fputw(uword_t w, FILE *f)
{
    fputh((long)((w >> 24) & 0xFFFFFF), f);
    fputh((long)(w & 0xFFFFFF), f);
}
