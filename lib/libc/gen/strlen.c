// From the BESM-6 c-compiler's own libc: libc/besm6/strlen.c -- taken rather than
// ported from v7, per lib/README.md.  Do not diverge without a reason written here.
//
// strlen — length of the NUL-terminated string s (C11 §7.24.6.3).
//
// Counts bytes up to, but not including, the terminating '\0'.  s is a fat
// char* cursor, so the scan crosses word boundaries on its own (b/pinc).
//
#include <string.h>

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p != 0) {
        p++;
    }
    return (size_t)(p - s);
}
