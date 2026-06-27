
#include <stdio.h>
#include <unistd.h>
#include "besm6/b.out.h"

// Read one int as two 24-bit half-words (6 bytes), matching fgetint():
// the value is the low (second) half-word big-endian, the high (first)
// half-word is ignored.
int getint(int f, int *i)
{
    unsigned char b[6];

    if (read(f, b, sizeof(b)) != (ssize_t) sizeof(b))
        return 0;
    *i = (b[3] << 16) | (b[4] << 8) | b[5];
    return 1;
}
