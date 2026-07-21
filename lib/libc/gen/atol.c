/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * Convert a string to a long.
 *
 * On the PDP-11 this was a copy of atoi() with a wider accumulator; here `long' IS
 * `int' -- one 48-bit word, 41 bits signed (doc/Besm6_Data_Representation.md) -- so
 * the two conversions cannot differ by so much as an overflow, and a second copy of
 * the loop would only be a second place to fix a bug.  One word is also all the gate
 * carries, which is why lseek and stime shed their second word in phase 1.
 */
#include <stdlib.h>

long atol(const char *p)
{
    return atoi(p);
}
