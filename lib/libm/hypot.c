// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// hypot(a, b) -- sqrt(a*a + b*b), carefully (C11 §7.12.7.3).
//
// v7's, unchanged.  The care is the point: the obvious spelling squares both
// arguments, and a*a overflows for any |a| above 2^31.5 even when the answer is
// perfectly representable -- and an overflow here is a fault, not an infinity.
// Dividing by the larger first keeps the ratio in [0,1] and the square in [1,2], so
// only the closing multiply can reach the top of the range, and it can only reach it
// when the answer itself is out of range.
//
// v7's cabs() and the struct complex it took go with the #if 0 they arrived in:
// there is no <complex.h> here (__STDC_NO_COMPLEX__ -- see cmd/cpp/cpp.c) and
// nothing declares cabs.
//
#include <math.h>

double hypot(double a, double b)
{
    double t;

    if (a < 0)
        a = -a;
    if (b < 0)
        b = -b;
    if (a > b) {
        t = a;
        a = b;
        b = t;
    }
    if (b == 0)
        return 0.;
    a /= b;
    return b * sqrt(1. + a * a);
}
