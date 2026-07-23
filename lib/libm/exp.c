/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * exp(x) -- e raised to the x (C11 §7.12.6.1).
 *
 * v7's rational fit (Hart & Cheney #1069, 22.35D), with the range gates rederived
 * for this machine's exponent.  The body is untouched: reduce x to an integer part
 * `ent' and a fraction in [-0.5,0.5], evaluate the fit on the fraction, and put the
 * integer part back with ldexp.
 *
 * THE TWO GATES ARE THE PORT.  v7 had a single `maxf' at DBL_MAX_10_EXP*2.5 = 45 and
 * checked |x| against it -- but exp(45) is e^45 ~ 3.5e19, past this machine's largest
 * finite 9.22e18, and computing it faults.  So the two ends are gated separately and
 * strictly, BEFORE the arithmetic:
 *
 *   overflow at x > 43.6.  ln(DBL_MAX) = 43.668, so 43.6 is the last tenth that still
 *   lands ent at 62 and keeps the closing ldexp in range.  Past it: ERANGE, HUGE_VAL.
 *
 *   underflow at x < -44.3.  ln(DBL_MIN) = -44.36; at -44.3 ent is -64, the lowest
 *   ldexp accepts, and the result has already flushed to machine zero.  Below it ent
 *   would run off the bottom and ldexp -- which keeps only the low 7 bits of its
 *   argument -- would wrap it to a huge exponent and return nonsense.  So the gate is
 *   not cosmetic: it is what keeps a tiny x from coming back enormous.  No errno: an
 *   underflow to zero is what the hardware does to any small value anyway.
 */
#include <errno.h>
#include <math.h>

static double p0 = .2080384346694663001443843411e7;
static double p1 = .3028697169744036299076048876e5;
static double p2 = .6061485330061080841615584556e2;
static double q0 = .6002720360238832528230907598e7;
static double q1 = .3277251518082914423057964422e6;
static double q2 = .1749287689093076403844945335e4;
static double log2e = 1.4426950408889634073599247;
static double sqrt2 = 1.4142135623730950488016887;
static double maxf = 43.6;  /* ln(DBL_MAX), rounded down */
static double minf = -44.3; /* ln(DBL_MIN), rounded up */

double exp(double arg)
{
    double fract;
    double temp1, temp2, xsq;
    int ent;

    if (arg == 0.)
        return 1.;
    if (arg < minf)
        return 0.;
    if (arg > maxf) {
        errno = ERANGE;
        return HUGE_VAL;
    }
    arg *= log2e;
    ent   = floor(arg);
    fract = (arg - ent) - 0.5;
    xsq   = fract * fract;
    temp1 = ((p2 * xsq + p1) * xsq + p0) * fract;
    temp2 = ((1.0 * xsq + q2) * xsq + q1) * xsq + q0;
    return ldexp(sqrt2 * (temp2 + temp1) / (temp2 - temp1), ent);
}
