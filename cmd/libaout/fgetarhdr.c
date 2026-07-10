
#include <stdio.h>
#include <stdlib.h>

#include "besm6/ar.h"
#include "besm6/b.out.h"

#define W 6 /* sizeof word of BESM-6 */

// Read one archive member header from a stream into *h, decoding the
// length-prefixed layout shared with getarhdr()/putarhdr(): a 1-byte name
// length L, L name bytes, zero padding up to a whole word, then one full word
// each for date, uid, gid, mode and size (each a 48-bit big-endian word; for
// uid/gid/mode the value is in the low half-word and the high half-word is
// discarded padding).  Allocates h->ar_name and NUL-terminates it; the caller
// must free() it.  Returns 1 on success, 0 on EOF, -1 on out of memory.
int fgetarhdr(FILE *f, struct ar_hdr *h)
{
    int i, len, pad;

    if ((len = getc(f)) == EOF)
        return 0;

    if (!(h->ar_name = malloc(len + 1)))
        return -1;
    for (i = 0; i < len; i++)
        h->ar_name[i] = getc(f);
    h->ar_name[len] = '\0';

    // Skip the zero padding that rounds (1 + len) up to a whole word.
    pad = (int)((W - (1 + len) % W) % W);
    for (i = 0; i < pad; i++)
        getc(f);

    h->ar_date = fgetw(f);
    h->ar_uid  = fgetw(f);
    h->ar_gid  = fgetw(f);
    h->ar_mode = fgetw(f);
    h->ar_size = fgetw(f);
    return 1;
}
