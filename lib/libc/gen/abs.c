// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// Absolute value of an int.
//
// abs(INT_MIN) is undefined here as it is everywhere: an int is 41 bits with its
// sign in bit 41 (doc/Besm6_Data_Representation.md), so negating the most negative
// one yields itself.
//
#include <stdlib.h>

int abs(int arg)
{
    if (arg < 0)
        arg = -arg;
    return arg;
}
