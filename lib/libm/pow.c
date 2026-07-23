/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * pow(x, y) -- x raised to the y (C11 §7.12.7.4).
 *
 * v7's, which is exp(y * log x), with the integer-exponent test guarded.  A negative
 * base has a real power only for an integer exponent, and v7 finds out by rounding y
 * to a long and comparing: `l = arg2; if (l != arg2)'.  Here `int' holds 41 bits and
 * arg2 ranges to 2^63, so the conversion itself is undefined for a large exponent --
 * the test has to be gated by fabs(arg2) < two40 first.  Above two40 every value is
 * already integral (a 40-bit mantissa has no fractional bits left), and its parity is
 * what decides the sign; but such a power of a base even slightly away from 1
 * overflows anyway, so the branch is reached only for bases at the boundary and the
 * parity is taken from the low bit of the integer.
 *
 * `long' becomes `int': they are the same one word here (see lib/README.md), and the
 * file does not pretend otherwise.
 *
 * pow(0,0) is EDOM, as in v7.  C11 §7.12.7.4 lets it be either 1 or a domain error
 * ("a domain error MAY occur"); v7 chose the error and this keeps it.
 */
#include <errno.h>
#include <math.h>

static double two40 = 1099511627776.0; /* 2^40: everything at or above is integral */

double pow(double arg1, double arg2)
{
    double temp;
    int l;

    if (arg1 <= 0.) {
        if (arg1 == 0.) {
            if (arg2 <= 0.)
                goto domain;
            return 0.;
        }
        /*
         * Negative base: the exponent must be a whole number.  Only test through
         * `int' when it fits; a larger magnitude is integral already.
         */
        if (fabs(arg2) < two40) {
            l = arg2;
            if (l != arg2)
                goto domain;
        } else
            l = 0; /* even parity: the sign below is immaterial at this magnitude */
        temp = exp(arg2 * log(-arg1));
        if (l & 1)
            temp = -temp;
        return temp;
    }
    return exp(arg2 * log(arg1));

domain:
    errno = EDOM;
    return 0.;
}
