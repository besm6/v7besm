/* vprintf -- printf on an already-started va_list (C11 §7.21.6.10). */
#include <stdio.h>

int vprintf(const char *fmt, va_list ap)
{
    return vfprintf(stdout, fmt, ap);
}
