/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

#include <stdio.h>

int fgetc(FILE *iop)
{
    return getc(iop);
}
