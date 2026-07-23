
#include <stdio.h>
#include <string.h>

#include "besm6/ar.h"

#define W 6 // sizeof word of BESM-6

// On-disk size in bytes of an archive member header for the name in *h:
// a 1-byte length, the name bytes, zero padding up to a whole word, then five
// 48-bit metadata words (date, uid, gid, mode, size).  Always a multiple of W,
// so the member data that follows stays word-aligned.
int arhdrsz(const struct ar_hdr *h)
{
    size_t namelen = h->ar_name ? strlen(h->ar_name) : 0;

    return (int)(((1 + namelen + W - 1) / W) * W + 5 * W);
}
