/* From the BESM-6 c-compiler's own libc: libc/besm6/strcmp.c -- taken rather than
   ported from v7, per lib/README.md.  Do not diverge without a reason written here. */
/*
 * strcmp — compare two NUL-terminated strings (C11 §7.24.4.2).
 *
 * Returns <0, 0, or >0 according to whether s1 is less than, equal to, or
 * greater than s2, comparing the first differing bytes as unsigned char.
 */
#include <string.h>

int strcmp(const char *s1, const char *s2)
{
    const unsigned char *a = (unsigned char *)s1;
    const unsigned char *b = (unsigned char *)s2;
    while (*a != 0 && *a == *b) {
        a++;
        b++;
    }
    return (int)*a - (int)*b;
}
