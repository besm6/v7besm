/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * tanh(x) -- the hyperbolic tangent (C11 §7.12.5.6).
 *
 * v7's, with the saturation threshold rederived.  For a large argument tanh is 1 to
 * the last bit and v7 returns the sign outright rather than form sinh/cosh, which
 * would overflow.  v7's cutoff is 21 (its 56-bit mantissa); here tanh saturates once
 * 2*exp(-2x) falls below 2^-40, at 20*ln2 = 13.86, so the cutoff is 14 -- the same
 * point sinh and cosh switch on.  Below it, tanh is sinh/cosh, both of which stay in
 * range there.
 */
#include <math.h>

double tanh(double arg)
{
    double sign;

    sign = 1.;
    if (arg < 0.) {
        arg  = -arg;
        sign = -1.;
    }

    if (arg > 14.)
        return sign;

    return sign * sinh(arg) / cosh(arg);
}
