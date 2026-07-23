// From the BESM-6 c-compiler's own libc: libc/besm6/strncat.c -- taken rather than
// ported from v7, per lib/README.md.  Do not diverge without a reason written here.
//
// strncat — append at most n bytes of src to dest (C11 §7.24.3.2).
//
// Copies up to n characters from src, then always writes a terminating '\0'.
// The objects must not overlap.
//
#include <string.h>

char *strncat(char *dest, const char *src, size_t n)
{
    char *d       = dest;
    const char *s = src;
    while (*d != 0) {
        d++;
    }
    while (n > 0 && *s != 0) {
        *d = *s;
        d++;
        s++;
        n--;
    }
    *d = 0;
    return dest;
}
