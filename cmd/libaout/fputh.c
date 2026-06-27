
#include <stdio.h>
#include "besm6/b.out.h"

// One half-word == 24 bits == 3 bytes on a 48-bit BESM-6 word.
void fputh(long h, FILE *f)
{
    putc((int) h, f);
    putc((int) (h >> 8), f);
    putc((int) (h >> 16), f);
}
