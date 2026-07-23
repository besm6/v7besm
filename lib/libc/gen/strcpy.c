// From the BESM-6 c-compiler's own libc: libc/besm6/strcpy.c -- taken rather than
// ported from v7, per lib/README.md.  Do not diverge without a reason written here.
//
// strcpy — copy the NUL-terminated string src into dest (C11 §7.24.2.3).
//
// Copies up to and including the terminating '\0'.  The objects must not
// overlap.  dest/src are fat char* cursors advancing across word boundaries.
//
#include <string.h>

char *strcpy(char *dest, const char *src)
{
    char *d       = dest;
    const char *s = src;
    while (*s != 0) {
        *d = *s;
        d++;
        s++;
    }
    *d = 0;
    return dest;
}
