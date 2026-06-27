
#include <stdio.h>
#include "besm6/b.out.h"

// Read one int from a stream as two 24-bit half-words (6 bytes == one word):
// the value is the low half-word, the high half-word is read and ignored.
// Always returns 1.
int fgetint(register FILE *f, register int *i)
{
    *i = fgeth(f);
    fgeth(f);
    return 1;
}
