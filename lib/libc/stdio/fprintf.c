/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

#include <stdio.h>

int fprintf(FILE *iop, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vfprintf(iop, fmt, ap);
    va_end(ap);
    return n;
}
