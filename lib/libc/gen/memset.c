// From the BESM-6 c-compiler's own libc: libc/besm6/memset.c -- taken rather than
// ported from v7, per lib/README.md.  Do not diverge without a reason written here.
//
// memset — fill the first n bytes of s with the byte value c (C11 §7.24.6.1).
//
// c is converted to unsigned char before storing.  s is traversed as a fat
// char* cursor, so the fill crosses word boundaries on its own (b/pinc).
//
#include <string.h>

void *memset(void *s, int c, size_t n)
{
    char *p = s;
    char b  = (char)c; // value stored is (unsigned char)c
    while (n > 0) {
        *p = b;
        p++;
        n--;
    }
    return s;
}
