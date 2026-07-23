// vscanf -- scanf on an already-started va_list (C11 §7.21.6.13).
#include <stdio.h>

int vscanf(const char *fmt, va_list ap)
{
    return vfscanf(stdin, fmt, ap);
}
