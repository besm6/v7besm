/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * fabs(x) -- the magnitude of x (C11 §7.12.7.2).
 *
 * v7's three lines, converted to a prototype.  There is no sign bit to clear here:
 * the mantissa is two's complement, so negation is arithmetic and not a bit trick,
 * and there is no negative zero for the comparison to get wrong.
 */
#include <math.h>

double fabs(double x)
{
    if (x < 0.)
        x = -x;
    return x;
}
