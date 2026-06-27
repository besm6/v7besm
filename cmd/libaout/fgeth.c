
#include <stdio.h>
#include "besm6/b.out.h"

// One half-word == 24 bits == 3 bytes on a 48-bit BESM-6 word.
long fgeth(register FILE *f)
{
    register long h;

    h = getc(f);
    h |= getc(f) << 8;
    return h | (long) getc(f) << 16;
}
