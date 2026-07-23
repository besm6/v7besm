/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * log(x) -- the natural logarithm; log10(x) -- the base-10 logarithm
 * (C11 §7.12.6.7, §7.12.6.10).
 *
 * v7's, unchanged but for prototypes.  The fit (Hart & Cheney #2705, 19.38D) works on
 * a mantissa reduced to [sqrt(1/2), sqrt(2)] and adds back exp*ln2, so nothing here
 * grows toward the exponent limit and no range gate is needed at the top.  The only
 * error case is the domain one: log of a non-positive number is EDOM, and the value
 * returned is -HUGE_VAL, the most negative finite value rather than a -infinity there
 * is no room for.
 *
 * log10 is log times 1/ln10, in its own entry point but the same file, as v7 keeps
 * it.
 */
#include <errno.h>
#include <math.h>

static double _log2 = 0.693147180559945309e0;
static double ln10 = 2.302585092994045684;
static double sqrto2 = 0.707106781186547524e0;
static double p0 = -.240139179559210510e2;
static double p1 = 0.309572928215376501e2;
static double p2 = -.963769093368686593e1;
static double p3 = 0.421087371217979714e0;
static double q0 = -.120069589779605255e2;
static double q1 = 0.194809660700889731e2;
static double q2 = -.891110902798312337e1;

double log(double arg)
{
    double x, z, zsq, temp;
    int dexp;

    if (arg <= 0.) {
        errno = EDOM;
        return -HUGE_VAL;
    }
    x = frexp(arg, &dexp);
    while (x < 0.5) {
        x    = x * 2;
        dexp = dexp - 1;
    }
    if (x < sqrto2) {
        x    = 2 * x;
        dexp = dexp - 1;
    }

    z   = (x - 1) / (x + 1);
    zsq = z * z;

    temp = ((p3 * zsq + p2) * zsq + p1) * zsq + p0;
    temp = temp / (((1.0 * zsq + q2) * zsq + q1) * zsq + q0);
    temp = temp * z + dexp * _log2;
    return temp;
}

double log10(double arg)
{
    return log(arg) / ln10;
}
