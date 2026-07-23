// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// Fold one character to lower case, C11 SS7.4.2.1: the argument back unchanged
// unless it is an upper-case letter.  See toupper.c for why this is a function.
//
#include <ctype.h>

int tolower(int c)
{
    return isupper(c) ? _tolower(c) : c;
}
