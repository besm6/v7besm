/* snprintf -- format into a bounded buffer (C11 §7.21.6.5).  Not a v7 routine. */
#include <stdio.h>

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}
