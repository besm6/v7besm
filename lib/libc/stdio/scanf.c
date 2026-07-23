/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

#include <stdio.h>

int scanf(const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vfscanf(stdin, fmt, ap);
    va_end(ap);
    return n;
}
