/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * asin(x), acos(x) -- the inverse sine and cosine (C11 §7.12.4.2, §7.12.4.1).
 *
 * v7's, unchanged but for prototypes.  Both reduce to atan after a range check: an
 * argument outside [-1,1] is a domain error (EDOM, return 0), which is the whole of
 * the error handling here -- nothing grows toward the exponent limit.
 */
#include <errno.h>
#include <math.h>

static double pio2 = 1.570796326794896619;

double asin(double arg)
{
    double sign, temp;

    sign = 1.;
    if (arg < 0) {
        arg  = -arg;
        sign = -1.;
    }

    if (arg > 1.) {
        errno = EDOM;
        return 0.;
    }

    temp = sqrt(1. - arg * arg);
    if (arg > 0.7)
        temp = pio2 - atan(temp / arg);
    else
        temp = atan(arg / temp);

    return sign * temp;
}

double acos(double arg)
{
    if (arg < 0)
        arg = -arg;

    if (arg > 1.) {
        errno = EDOM;
        return 0.;
    }

    return pio2 - asin(arg);
}
