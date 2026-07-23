// From the BESM-6 c-compiler's own libc: libc/besm6/memmove.c -- taken rather than
// ported from v7, per lib/README.md.  Do not diverge without a reason written here.
//
// memmove — copy n bytes from src to dest, overlap-safe (C11 §7.24.2.2).
//
// Unlike memcpy, the regions may overlap.  When dest precedes src the copy
// runs forward; otherwise it runs backward (high index to low) so a byte is
// read before it is overwritten.  d/s are fat char* cursors.
//
#include <string.h>

void *memmove(void *dest, const void *src, size_t n)
{
    char *d       = dest;
    const char *s = src;
    if (d < s) {
        size_t i = 0;
        while (i < n) {
            d[i] = s[i];
            i++;
        }
    } else {
        size_t i = n;
        while (i > 0) {
            i--;
            d[i] = s[i];
        }
    }
    return dest;
}
