// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

#include <stdio.h>

int fputc(int c, FILE *iop)
{
    return putc(c, iop);
}
