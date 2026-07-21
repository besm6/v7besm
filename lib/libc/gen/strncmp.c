/* From the BESM-6 c-compiler's own libc: libc/besm6/strncmp.c -- taken rather than
   ported from v7, per lib/README.md.  Do not diverge without a reason written here. */
/*
 * strncmp — compare at most n bytes of two strings (C11 §7.24.4.4).
 *
 * Like strcmp, but stops after n characters.  Returns <0, 0, or >0 from the
 * first differing byte (interpreted as unsigned char), or 0 if the first n
 * bytes are equal.
 */
#include <string.h>

int strncmp(const char *s1, const char *s2, size_t n)
{
    const unsigned char *a = (unsigned char *)s1;
    const unsigned char *b = (unsigned char *)s2;
    while (n > 0) {
        if (*a != *b) {
            return (int)*a - (int)*b;
        }
        if (*a == 0) {
            return 0;
        }
        a++;
        b++;
        n--;
    }
    return 0;
}
