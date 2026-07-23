//
// fgetpos -- record the current position (C11 §7.21.9.1).  Not a v7 routine.
//
// fpos_t is one word, the same off_t the kernel counts bytes in, so there is
// nothing opaque to hide behind and this is ftell() with the result stored through
// a pointer.
//
#include <stdio.h>

int fgetpos(FILE *iop, fpos_t *pos)
{
    long p;

    p = ftell(iop);
    if (p < 0)
        return -1;
    *pos = p;
    return 0;
}
