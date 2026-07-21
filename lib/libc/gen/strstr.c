/* From the BESM-6 c-compiler's own libc: libc/besm6/strstr.c -- taken rather than
   ported from v7, per lib/README.md.  Do not diverge without a reason written here. */
/*
 * strstr — locate the substring needle within haystack (C11 §7.24.5.7).
 *
 * Returns a pointer to the first occurrence of needle in haystack, or NULL if
 * not found.  An empty needle matches at the start of haystack.  Naive search.
 */
#include <string.h>

char *strstr(const char *haystack, const char *needle)
{
    if (*needle == 0) {
        return (char *)haystack;
    }
    const char *h = haystack;
    while (*h != 0) {
        const char *a = h;
        const char *b = needle;
        while (*a != 0 && *b != 0 && *a == *b) {
            a++;
            b++;
        }
        if (*b == 0) {
            return (char *)h;
        }
        h++;
    }
    return 0;
}
