// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// floor(x) -- the greatest integer <= x; ceil(x) -- the least integer >= x
// (C11 §7.12.9.2, §7.12.9.1).
//
// v7's, unchanged but for the prototypes.  Both stand on modf(), which is in libc
// rather than here because the conversions need it (lib/libc/gen/modf.c), and modf
// already knows that any magnitude at or above 2^40 is integral in a 40-bit mantissa
// -- so a large argument comes back with a zero fraction and falls through both
// routines untouched.
//
// The two share a file because ceil() is one call to floor(), which is v7's own
// arrangement.
//
#include <math.h>

double floor(double d)
{
    double fract;

    if (d < 0.0) {
        d     = -d;
        fract = modf(d, &d);
        if (fract != 0.0)
            d += 1;
        d = -d;
    } else
        modf(d, &d);
    return d;
}

double ceil(double d)
{
    return -floor(-d);
}
