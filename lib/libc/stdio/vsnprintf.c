/*
 * vsnprintf -- format into a bounded buffer (C11 §7.21.6.12), and with it the
 * whole sprintf family: vsprintf calls this one with a nominal size.
 *
 * The sink is v7's _IOSTRG stream -- a FILE on the stack over the caller's buffer,
 * with no descriptor behind it.  When the count runs out _flsbuf() sees _IOSTRG,
 * DROPS the byte and leaves the count at zero (stdio/flsbuf.c), so every later putc
 * lands there too; the engine goes on counting characters it could not store, and
 * that count is what §7.21.6.12 says to return.
 *
 * size == 0 writes nothing at all, not even the NUL, which is what the standard
 * requires and is why the terminator is written through _ptr rather than at a
 * computed offset: _ptr is where the stored characters actually stopped.
 */
#include <stdio.h>

int _doprnt(const char *fmt, va_list ap, FILE *iop);

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    FILE strbuf;
    int n;

    strbuf._ptr    = buf;
    strbuf._base   = buf;
    strbuf._bufsiz = size;
    strbuf._cnt    = size > 0 ? (int)size - 1 : 0;
    strbuf._flag   = _IOWRT | _IOSTRG;
    strbuf._file   = -1;

    n = _doprnt(fmt, ap, &strbuf);

    if (size > 0)
        *strbuf._ptr = '\0';
    return n;
}
