/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

#include <stdio.h>

int sscanf(const char *str, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsscanf(str, fmt, ap);
    va_end(ap);
    return n;
}
