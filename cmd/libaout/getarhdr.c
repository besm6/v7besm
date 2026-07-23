
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "besm6/ar.h"
#include "besm6/b.out.h"

#define W 6 // sizeof word of BESM-6

// Decode one 48-bit word (6 bytes, big-endian) from a buffer.
#define GETW(p)                                                                    \
    (((uword_t)(p)[0] << 40) | ((uword_t)(p)[1] << 32) | ((uword_t)(p)[2] << 24) | \
     ((uword_t)(p)[3] << 16) | ((uword_t)(p)[4] << 8) | (uword_t)(p)[5])

// Read an archive header in the same length-prefixed layout as fgetarhdr():
// a 1-byte name length L, L name bytes, zero padding up to a whole word, then
// one full word each for date, uid, gid, mode and size (each a 48-bit
// big-endian word; for uid/gid/mode the value is in the low half-word and the
// high half-word is discarded padding).  Allocates h->ar_name and NUL-terminates
// it; the caller must free() it.  Returns 1 on success, 0 on a short read.
int getarhdr(int f, struct ar_hdr *h)
{
    unsigned char lb, b[ARMAXNAME + W + 5 * W];
    int len, rest, pad;

    if (read(f, &lb, 1) != 1)
        return 0;
    len = lb;

    // Remaining bytes after the length byte: name + word padding + 5 metadata
    // words.  arhdrsz() gives the whole header size for this name length.
    pad  = (int)((W - (1 + len) % W) % W);
    rest = len + pad + 5 * W;
    if (read(f, b, rest) != (ssize_t)rest)
        return 0;

    if (!(h->ar_name = malloc(len + 1)))
        return 0;
    for (int i = 0; i < len; i++)
        h->ar_name[i] = b[i];
    h->ar_name[len] = '\0';

    h->ar_date = GETW(b + len + pad);
    h->ar_uid  = GETW(b + len + pad + W);
    h->ar_gid  = GETW(b + len + pad + 2 * W);
    h->ar_mode = GETW(b + len + pad + 3 * W);
    h->ar_size = GETW(b + len + pad + 4 * W);
    return 1;
}
