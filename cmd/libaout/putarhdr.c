
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "besm6/ar.h"
#include "besm6/b.out.h"

#define W 6 /* sizeof word of BESM-6 */

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

// Write an archive member header in the length-prefixed layout that
// getarhdr()/fgetarhdr() read back (see getarhdr.c): a 1-byte name length L,
// L name bytes, zero padding up to a whole word, then the 5 metadata words.
// Only reads h->ar_name (may be a borrowed string).  Returns 1 on success.
int putarhdr(int f, const struct ar_hdr *h)
{
    unsigned char b[1 + ARMAXNAME + W + 5 * W];
    int len = (int)strlen(h->ar_name);
    int pad = (int)((W - (1 + len) % W) % W);
    int hdrsz;

    memset(b, 0, sizeof(b));
    b[0] = (unsigned char)len;
    for (int i = 0; i < len; i++)
        b[1 + i] = h->ar_name[i];

    putword(b + 1 + len + pad, h->ar_date);
    putword(b + 1 + len + pad + W, h->ar_uid);
    putword(b + 1 + len + pad + 2 * W, h->ar_gid);
    putword(b + 1 + len + pad + 3 * W, h->ar_mode);
    putword(b + 1 + len + pad + 4 * W, h->ar_size);

    hdrsz = 1 + len + pad + 5 * W;
    return write(f, b, hdrsz) == (ssize_t)hdrsz;
}
