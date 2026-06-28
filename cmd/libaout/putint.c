
#include <stdio.h>
#include <unistd.h>

#include "besm6/b.out.h"

// Write one full 48-bit word (6 bytes, big-endian) to a file descriptor.
int putint(int f, uword_t i)
{
    unsigned char b[6] = { i >> 40, i >> 32, i >> 24, i >> 16, i >> 8, i };

    return write(f, b, sizeof(b)) == (ssize_t)sizeof(b);
}
