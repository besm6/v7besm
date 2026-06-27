
#include <stdio.h>
#include <unistd.h>
#include "besm6/b.out.h"

// Write one int as two 24-bit half-words (6 bytes), matching fgetint():
// the low half-word holds the value, the high half-word is zero.
int putint(int f, int i)
{
    unsigned char b[6] = { i, i >> 8, i >> 16, 0, 0, 0 };

    return write(f, b, sizeof(b)) == (ssize_t) sizeof(b);
}
