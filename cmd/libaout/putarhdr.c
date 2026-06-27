
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "besm6/b.out.h"
#include "besm6/ar.h"

// Encode one 24-bit half-word (3 bytes, big-endian) into a buffer.
static void puth(unsigned char *p, long v)
{
    p[0] = v >> 16;
    p[1] = v >> 8;
    p[2] = v;
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

    puth(b+16, h->ar_date >> 32);
    puth(b+19, h->ar_date);
    puth(b+25, h->ar_uid);
    puth(b+31, h->ar_gid);
    puth(b+37, h->ar_mode);
    puth(b+40, h->ar_size >> 32);
    puth(b+43, h->ar_size);
    return write(f, b, sizeof(b)) == (ssize_t) sizeof(b);
}
