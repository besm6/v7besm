// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// v7 keeps fread and fwrite in one file (stdio/rdwr.c); they are split here
// because they share nothing, and one function per file is what lets b6ranlib's
// index pull only the one a program calls.
//
#include <stdio.h>

size_t fread(void *ptr, size_t size, size_t count, FILE *iop)
{
    char *p;
    int c;
    size_t ndone, s;

    p     = ptr;
    ndone = 0;
    if (size)
        for (; ndone < count; ndone++) {
            s = size;
            do {
                if ((c = getc(iop)) >= 0)
                    *p++ = c;
                else
                    return ndone;
            } while (--s);
        }
    return ndone;
}
