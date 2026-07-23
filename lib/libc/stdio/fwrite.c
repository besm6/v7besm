/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/* See fread.c for why v7's rdwr.c is two files here. */
#include <stdio.h>

size_t fwrite(const void *ptr, size_t size, size_t count, FILE *iop)
{
    const char *p;
    size_t ndone, s;

    p     = ptr;
    ndone = 0;
    if (size)
        for (; ndone < count; ndone++) {
            s = size;
            do {
                putc(*p++, iop);
            } while (--s);
            if (ferror(iop))
                break;
        }
    return ndone;
}
