
#include <stdio.h>
#include "besm6/b.out.h"
#include "besm6/ar.h"

// Read one archive member header from a stream into *h, decoding the 46-byte
// on-disk layout shared with getarhdr()/putarhdr(): 14 name bytes, 2 zero
// bytes, then one full word each for date, uid, gid, mode and size (each a
// 48-bit big-endian word; for uid/gid/mode the value is in the low half-word
// and the high half-word is discarded padding).
// Returns 1 on success, 0 on EOF or a non-zero byte where padding is expected.
int fgetarhdr(register FILE *f, register struct ar_hdr *h)
{
    register int i;

    for (i=0; i<14; i++) {
        register int c;
        if ((c = getc(f)) == EOF)
            return 0;
        h->ar_name[i] = c;
    }
    if (getc(f))
            return 0;
    if (getc(f))
            return 0;

    h->ar_date = fgetw(f);
    h->ar_uid  = fgetw(f);
    h->ar_gid  = fgetw(f);
    h->ar_mode = fgetw(f);
    h->ar_size = fgetw(f);
    return 1;
}
