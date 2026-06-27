
#include <stdio.h>
#include "besm6/b.out.h"

// Read one int from a stream as a single full word (6 bytes): the value is the
// low half-word, the high half-word is the discarded padding. Always returns 1.
int fgetint(register FILE *f, register int *i)
{
    *i = fgetw(f);
    return 1;
}
