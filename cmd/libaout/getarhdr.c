
#include <stdio.h>
#include <unistd.h>
#include "besm6/b.out.h"
#include "besm6/ar.h"

// Decode one 24-bit half-word (3 bytes, little-endian) from a buffer.
#define GETH(p) ((long)(p)[0] | ((long)(p)[1] << 8) | ((long)(p)[2] << 16))

// Read an archive header in the same field layout as fgetarhdr():
// 14 name bytes, 2 zero bytes, then two-half-word date, single-half-word
// uid/gid/mode (each followed by a discarded half-word) and a two-half-word
// size -- 46 bytes total.
int getarhdr(int f, struct ar_hdr *h)
{
    unsigned char b[46];
    register int i;

    if (read(f, b, sizeof(b)) != (ssize_t) sizeof(b))
        return 0;

    for (i=0; i<14; i++)
        h->ar_name[i] = b[i];

    h->ar_date = GETH(b+16) | (GETH(b+19) << 32);
    h->ar_uid  = GETH(b+22);
    h->ar_gid  = GETH(b+28);
    h->ar_mode = GETH(b+34);
    h->ar_size = GETH(b+40) | (GETH(b+43) << 32);
    return 1;
}
