
#include <stdio.h>
#include <unistd.h>

#include "besm6/b.out.h"

// Read one full 48-bit word (6 bytes, big-endian) from a file descriptor,
// returning the complete value as a uword_t.
int getint(int f, uword_t *i)
{
    unsigned char b[6];

    if (read(f, b, sizeof(b)) != (ssize_t)sizeof(b))
        return 0;
    *i = ((uword_t)b[0] << 40) | ((uword_t)b[1] << 32) | ((uword_t)b[2] << 24) |
         ((uword_t)b[3] << 16) | ((uword_t)b[4] << 8) | (uword_t)b[5];
    return 1;
}
