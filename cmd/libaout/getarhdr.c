
#include <stdio.h>
#include <unistd.h>
#include "besm6/b.out.h"
#include "besm6/ar.h"

// Decode one 48-bit word (6 bytes, big-endian) from a buffer.
#define GETW(p) (((uword_t)(p)[0] << 40) | ((uword_t)(p)[1] << 32) | \
                 ((uword_t)(p)[2] << 24) | ((uword_t)(p)[3] << 16) | \
                 ((uword_t)(p)[4] << 8)  |  (uword_t)(p)[5])

// Read an archive header in the same field layout as fgetarhdr():
// 14 name bytes, 2 zero bytes, then one full word each for date, uid, gid, mode
// and size (each a 48-bit big-endian word; for uid/gid/mode the value is in the
// low half-word and the high half-word is discarded padding) -- 46 bytes total.
int getarhdr(int f, struct ar_hdr *h)
{
    unsigned char b[46];
    register int i;

    if (read(f, b, sizeof(b)) != (ssize_t) sizeof(b))
        return 0;

    for (i=0; i<14; i++)
        h->ar_name[i] = b[i];

    h->ar_date = GETW(b+16);
    h->ar_uid  = GETW(b+22);
    h->ar_gid  = GETW(b+28);
    h->ar_mode = GETW(b+34);
    h->ar_size = GETW(b+40);
    return 1;
}
