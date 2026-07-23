/*
 * vsscanf -- scan a string (C11 §7.21.6.14), and with it sscanf.
 *
 * The source is v7's _IOSTRG stream: a FILE on the stack over the caller's string,
 * with no descriptor behind it.  _filbuf() returns EOF for such a stream
 * (stdio/filbuf.c), so the scan stops at the NUL and never reaches a syscall.
 */
#include <stdio.h>

int _doscan(FILE *iop, const char *fmt, va_list ap);

int vsscanf(const char *str, const char *fmt, va_list ap)
{
    FILE strbuf;
    const char *p;
    int n;

    n = 0;
    for (p = str; *p; p++)
        n++;

    strbuf._ptr    = (char *)str;
    strbuf._base   = (char *)str;
    strbuf._cnt    = n;
    strbuf._bufsiz = n;
    strbuf._flag   = _IOREAD | _IOSTRG;
    strbuf._file   = -1;

    return _doscan(&strbuf, fmt, ap);
}
