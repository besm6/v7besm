
#include <stdio.h>
#include <unistd.h>
#include "besm6/b.out.h"
#include "besm6/ar.h"

// Decode one 24-bit half-word (3 bytes, big-endian) from a buffer.
#define GETH(p) (((long)(p)[0] << 16) | ((long)(p)[1] << 8) | (long)(p)[2])

// Read an archive header in the same field layout as fgetarhdr():
// 14 name bytes, 2 zero bytes, then one full 48-bit word each for date, uid,
// gid, mode and size (for uid/gid/mode the value is the low half-word, preceded
// by a discarded high half-word) -- 46 bytes total.
int getarhdr(int f, struct ar_hdr *h)
{
    unsigned char b[46];
    register int i;

    if (read(f, b, sizeof(b)) != (ssize_t) sizeof(b))
        return 0;

    for (i=0; i<14; i++)
        h->ar_name[i] = b[i];

    h->ar_date = ((uword_t) GETH(b+16) << 24) | GETH(b+19);
    h->ar_uid  = GETH(b+25);
    h->ar_gid  = GETH(b+31);
    h->ar_mode = GETH(b+37);
    h->ar_size = ((uword_t) GETH(b+40) << 24) | GETH(b+43);
    return 1;
}
