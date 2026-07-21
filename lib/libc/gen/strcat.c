/* From the BESM-6 c-compiler's own libc: libc/besm6/strcat.c -- taken rather than
   ported from v7, per lib/README.md.  Do not diverge without a reason written here. */
/*
 * strcat — append src to the NUL-terminated string dest (C11 §7.24.3.1).
 *
 * The initial '\0' of dest is overwritten by the first byte of src; a new '\0'
 * terminates the result.  The objects must not overlap.
 */
#include <string.h>

char *strcat(char *dest, const char *src)
{
    char *d       = dest;
    const char *s = src;
    while (*d != 0) {
        d++;
    }
    while (*s != 0) {
        *d = *s;
        d++;
        s++;
    }
    *d = 0;
    return dest;
}
