// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// Fold one character to upper case, C11 SS7.4.2.2: the argument back unchanged
// unless it is a lower-case letter.
//
// A function and not a macro, which is the one place this <ctype.h> departs from
// v7's.  v7's toupper() was the unconditional `(c) - 'a' + 'A'', so v7's
// toupper('1') returned garbage; the conditional form has to test the class and
// then yield the argument, i.e. evaluate it twice, and SS7.1.4 forbids a library
// macro from doing that.  v7's unconditional pair survives under v7's own name
// for it, _toupper/_tolower, for the callers that have already checked.
//
#include <ctype.h>

int toupper(int c)
{
    return islower(c) ? _toupper(c) : c;
}
