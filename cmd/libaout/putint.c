
#include <stdio.h>
#include <unistd.h>
#include "besm6/b.out.h"

// Write one int as two 24-bit half-words (6 bytes), matching fgetint():
// the high (first) half-word is zero, the low (second) half-word holds the
// value big-endian.
int putint(int f, int i)
{
    unsigned char b[6] = { 0, 0, 0, i >> 16, i >> 8, i };

    return write(f, b, sizeof(b)) == (ssize_t) sizeof(b);
}
