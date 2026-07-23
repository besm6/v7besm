/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * sinh(x), cosh(x) -- the hyperbolic sine and cosine (C11 §7.12.5.5, §7.12.5.3).
 *
 * v7's, with the large-argument threshold rederived and the large-argument arithmetic
 * respelled so it does not overflow prematurely.
 *
 * THE THRESHOLD.  v7 drops the exp(-x) term at |x| > 21, which is where it stops
 * contributing to a 56-bit PDP-11 mantissa.  Here the mantissa is 40 bits, so the
 * term dies at 20*ln2 = 13.86 -- past |x| = 14, exp(-2x) is already below 2^-40 and
 * exp(x) - exp(-x) and exp(x) agree to the last bit.
 *
 * THE ARITHMETIC.  For the large branch v7 writes exp(x)/2.  Spelled that way here it
 * would report the wrong error: exp() faults its own argument at 43.6 (ln DBL_MAX), so
 * exp(x)/2 would come back as a saturated HUGE_VAL/2 for x in (43.6, 44.3] where the
 * true sinh -- half of exp(x) -- is still perfectly representable.  Writing it as
 * exp(x - ln2), which is identically exp(x)/2, moves exp()'s gate to x = 44.3
 * (ln 2*DBL_MAX) -- exactly where sinh itself overflows -- and lets exp() raise the
 * ERANGE at the right place.
 */
#include <math.h>

static double ln2 = 0.69314718055994531;

static double p0 = -0.6307673640497716991184787251e+6;
static double p1 = -0.8991272022039509355398013511e+5;
static double p2 = -0.2894211355989563807284660366e+4;
static double p3 = -0.2630563213397497062819489e+2;
static double q0 = -0.6307673640497716991212077277e+6;
static double q1 = 0.1521517378790019070696485176e+5;
static double q2 = -0.173678953558233699533450911e+3;

double sinh(double arg)
{
    double temp, argsq;
    int sign;

    sign = 1;
    if (arg < 0) {
        arg  = -arg;
        sign = -1;
    }

    if (arg > 14.) {
        temp = exp(arg - ln2); /* = exp(arg)/2; exp() raises ERANGE past 44.3 */
        return sign > 0 ? temp : -temp;
    }

    if (arg > 0.5)
        return sign * (exp(arg) - exp(-arg)) / 2;

    argsq = arg * arg;
    temp  = (((p3 * argsq + p2) * argsq + p1) * argsq + p0) * arg;
    temp /= (((argsq + q2) * argsq + q1) * argsq + q0);
    return sign * temp;
}

double cosh(double arg)
{
    if (arg < 0)
        arg = -arg;
    if (arg > 14.)
        return exp(arg - ln2); /* = exp(arg)/2; see sinh */

    return (exp(arg) + exp(-arg)) / 2;
}
