// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// Write one WORD, six bytes, most significant first.  See getw.c.  v7's putw
// returned nothing; this one answers EOF if the stream went bad.
//
#include <stdio.h>

int putw(int w, FILE *iop)
{
    unsigned u;
    int i;

    u = w;
    for (i = (int)sizeof(int) - 1; i >= 0; i--)
        putc((u >> (8 * i)) & 0377, iop);
    return ferror(iop) ? EOF : 0;
}
