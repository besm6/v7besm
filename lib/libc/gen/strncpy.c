/* From the BESM-6 c-compiler's own libc: libc/besm6/strncpy.c -- taken rather than
   ported from v7, per lib/README.md.  Do not diverge without a reason written here. */
/*
 * strncpy — copy at most n bytes of src into dest (C11 §7.24.2.4).
 *
 * Copies characters from src up to n.  If src is shorter than n, the remainder
 * of dest is padded with '\0'.  If src is n or longer, no terminating '\0' is
 * written.  dest/src are fat char* cursors.
 */
#include <string.h>

char *strncpy(char *dest, const char *src, size_t n)
{
    char *d       = dest;
    const char *s = src;
    while (n > 0 && *s != 0) {
        *d = *s;
        d++;
        s++;
        n--;
    }
    while (n > 0) {
        *d = 0;
        d++;
        n--;
    }
    return dest;
}
