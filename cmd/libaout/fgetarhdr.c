
#include <stdio.h>

#include "besm6/ar.h"
#include "besm6/b.out.h"

// Read one archive member header from a stream into *h, decoding the 60-byte
// on-disk layout shared with getarhdr()/putarhdr(): 30 name bytes (5 words),
// then one full word each for date, uid, gid, mode and size (each a 48-bit
// big-endian word; for uid/gid/mode the value is in the low half-word and the
// high half-word is discarded padding).
// Returns 1 on success, 0 on EOF.
int fgetarhdr(FILE *f, struct ar_hdr *h)
{
    int i;

    for (i = 0; i < (int)sizeof(h->ar_name); i++) {
        int c;
        if ((c = getc(f)) == EOF)
            return 0;
        h->ar_name[i] = c;
    }

    h->ar_date = fgetw(f);
    h->ar_uid  = fgetw(f);
    h->ar_gid  = fgetw(f);
    h->ar_mode = fgetw(f);
    h->ar_size = fgetw(f);
    return 1;
}
