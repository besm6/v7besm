
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "besm6/b.out.h"
#include "besm6/ar.h"

// Encode one 48-bit word (6 bytes, big-endian) into a buffer.
static void putword(unsigned char *p, uword_t v)
{
    p[0] = v >> 40;
    p[1] = v >> 32;
    p[2] = v >> 24;
    p[3] = v >> 16;
    p[4] = v >> 8;
    p[5] = v;
}

// Write an archive header in the same field layout getarhdr()/fgetarhdr()
// read back -- 46 bytes total (see getarhdr.c for the layout).
int putarhdr(int f, const struct ar_hdr *h)
{
    unsigned char b[46];
    register int i;

    memset(b, 0, sizeof(b));
    for (i=0; i<14; i++)
        b[i] = h->ar_name[i];

    putword(b+16, h->ar_date);
    putword(b+22, h->ar_uid);
    putword(b+28, h->ar_gid);
    putword(b+34, h->ar_mode);
    putword(b+40, h->ar_size);
    return write(f, b, sizeof(b)) == (ssize_t) sizeof(b);
}
