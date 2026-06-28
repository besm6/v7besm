
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "besm6/ar.h"
#include "besm6/b.out.h"

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
// read back -- 60 bytes total (see getarhdr.c for the layout).
int putarhdr(int f, const struct ar_hdr *h)
{
    unsigned char b[60];
    int i;

    memset(b, 0, sizeof(b));
    for (i = 0; i < (int)sizeof(h->ar_name); i++)
        b[i] = h->ar_name[i];

    putword(b + 30, h->ar_date);
    putword(b + 36, h->ar_uid);
    putword(b + 42, h->ar_gid);
    putword(b + 48, h->ar_mode);
    putword(b + 54, h->ar_size);
    return write(f, b, sizeof(b)) == (ssize_t)sizeof(b);
}
