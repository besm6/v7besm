
#include <stdio.h>
#include <unistd.h>
#include "besm6/b.out.h"

// Read one int as two 24-bit half-words (6 bytes), matching fgetint():
// the value is the low half-word, the high half-word is ignored.
int getint(int f, int *i)
{
    unsigned char b[6];

    if (read(f, b, sizeof(b)) != (ssize_t) sizeof(b))
        return 0;
    *i = b[0] | (b[1] << 8) | (b[2] << 16);
    return 1;
}
