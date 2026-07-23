// From the BESM-6 c-compiler's own libc: libc/besm6/memcmp.c -- taken rather than
// ported from v7, per lib/README.md.  Do not diverge without a reason written here.
//
// memcmp — compare the first n bytes of s1 and s2 (C11 §7.24.4.1).
//
// Returns <0, 0, or >0 according to whether the first differing byte in s1 is
// less than, equal to, or greater than the corresponding byte in s2, with the
// bytes interpreted as unsigned char.  s1/s2 are fat char* cursors.
//
#include <string.h>

int memcmp(const void *s1, const void *s2, size_t n)
{
    const unsigned char *a = s1;
    const unsigned char *b = s2;
    while (n > 0) {
        if (*a != *b) {
            return (int)*a - (int)*b;
        }
        a++;
        b++;
        n--;
    }
    return 0;
}
