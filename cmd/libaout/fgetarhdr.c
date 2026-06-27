
#include <stdio.h>
#include "besm6/b.out.h"
#include "besm6/ar.h"

// Read one archive member header from a stream into *h, decoding the 46-byte
// on-disk layout shared with getarhdr()/putarhdr(): 14 name bytes, 2 zero
// bytes, a two-half-word date (high half-word first), one full word each for
// uid/gid/mode (the value in the low half-word, the high half-word discarded)
// and a two-half-word size.
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

    h->ar_date = (uword_t) fgeth(f) << 32;
    h->ar_date |= fgeth(f);

    h->ar_uid = fgetw(f);
    h->ar_gid = fgetw(f);
    h->ar_mode = fgetw(f);

    h->ar_size = (uword_t) fgeth(f) << 32;
    h->ar_size |= fgeth(f);
    return 1;
}
