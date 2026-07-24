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

//
// Copy string a to b, and return a pointer to the NUL that was written -- NOT to the
// start of b, as strcpy would.  That is what lets copies be chained: the result of one
// movstr() is where the next one starts writing, so a name, an `=' and a value can be
// laid down end to end without measuring anything.
//
STRING movstr(STRING a, STRING b)
{
    while ((*b++ = *a++) != 0)
        ;
    return --b;
}

//
// Does character c occur anywhere in string s?  Used mostly to ask whether a character
// is one of the word separators in IFS.
//
INT any(CHAR c, STRING s)
{
    CHAR d;

    while ((d = *s++) != 0) {
        if (d == c)
            return TRUE;
    }
    return FALSE;
}

//
// Compare two strings, like strcmp: negative if s1 sorts first, 0 if they are equal,
// positive otherwise.  The `eq' macro in defs.h is this against 0.
//
INT cf(STRING s1, STRING s2)
{
    while (*s1++ == *s2) {
        if (*s2++ == 0)
            return 0;
    }
    return *--s1 - *s2;
}

//
// The length of a string INCLUDING its terminating NUL -- one more than strlen would
// say.  Callers rely on that: prs() prints length(s)-1 characters, and make() asks for
// length(v) bytes and gets room for the NUL as well.  A null pointer counts as 0.
//
INT length(STRING as)
{
    STRING s = as;

    if (s != 0) {
        while (*s++ != 0)
            ;
    }
    return s - as;
}
