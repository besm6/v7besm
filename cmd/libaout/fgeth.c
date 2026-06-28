
#include <stdio.h>
#include "besm6/b.out.h"

// One half-word == 24 bits == 3 bytes on a 48-bit BESM-6 word, stored
// big-endian (most significant byte first), natural for the BESM-6.
long fgeth(FILE *f)
{
    long h;

    h = (long) getc(f) << 16;
    h |= getc(f) << 8;
    return h | getc(f);
}
