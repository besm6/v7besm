// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// v7's sprintf returned the buffer; C11 §7.21.6.6 wants the character count, which
// is what <stdio.h> declares and what this returns.
//
#include <stdio.h>

int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsprintf(buf, fmt, ap);
    va_end(ap);
    return n;
}
