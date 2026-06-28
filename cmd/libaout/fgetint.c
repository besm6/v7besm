
#include <stdio.h>
#include "besm6/b.out.h"

// Read one int from a stream as a single full word (6 bytes): the value is the
// low (second) half-word, the high (first) half-word is the discarded padding.
// Always returns 1.
int fgetint(FILE *f, int *i)
{
    *i = fgetw(f);
    return 1;
}
