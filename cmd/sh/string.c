/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// General purpose string handling.
//
// The shell's own, not libc's, and the differences are relied on: movstr() returns a
// pointer to the NUL it wrote where strcpy() returns the destination, and length()
// COUNTS the NUL where strlen() does not.  prs() writes length(s)-1 bytes because of
// the second; name.c's make() allocates length(v) and copies with movstr() because of
// both.
//
#include "defs.h"

STRING movstr(STRING a, STRING b)
{
    while ((*b++ = *a++) != 0)
        ;
    return --b;
}

INT any(CHAR c, STRING s)
{
    CHAR d;

    while ((d = *s++) != 0) {
        if (d == c)
            return TRUE;
    }
    return FALSE;
}

INT cf(STRING s1, STRING s2)
{
    while (*s1++ == *s2) {
        if (*s2++ == 0)
            return 0;
    }
    return *--s1 - *s2;
}

INT length(STRING as)
{
    STRING s = as;

    if (s != 0) {
        while (*s++ != 0)
            ;
    }
    return s - as;
}
