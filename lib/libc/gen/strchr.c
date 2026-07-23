// From the BESM-6 c-compiler's own libc: libc/besm6/strchr.c -- taken rather than
// ported from v7, per lib/README.md.  Do not diverge without a reason written here.
//
// strchr — locate the first occurrence of c in s (C11 §7.24.5.2).
//
// c is converted to char.  The terminating '\0' is part of the string, so
// strchr(s, 0) returns a pointer to it.  Returns NULL if c is not found.
//
#include <string.h>

char *strchr(const char *s, int c)
{
    char ch       = (char)c;
    const char *p = s;
    for (;;) {
        if (*p == ch) {
            return (char *)p;
        }
        if (*p == 0) {
            return 0;
        }
        p++;
    }
}
