/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * atan(x) -- the inverse tangent in [-pi/2,pi/2]; atan2(y,x) -- the same in
 * [-pi,pi], sorted by quadrant (C11 §7.12.4.4, §7.12.4.3).
 *
 * v7's, unchanged but for prototypes and one typo: v7's first line reads
 * `double static sq2p1', which the parser here rejects -- the storage class has to
 * precede the type.  There are no error returns; the fit (Hart & Cheney #5077,
 * 19.56D) is evaluated on an argument reduced to [-0.414,0.414] and cannot overflow.
 *
 * atan2's `if ((arg1 + arg2) == arg1)' is v7's test for "arg2 is negligible beside
 * arg1", i.e. the angle is a right angle; it works here for the same reason it worked
 * on the PDP-11 -- adding a small enough value to a large one changes nothing -- and
 * needs no adjustment for the mantissa width.
 */
#include <math.h>

static double sq2p1 = 2.414213562373095048802e0;
static double sq2m1 = .414213562373095048802e0;
static double pio2 = 1.570796326794896619231e0;
static double pio4 = .785398163397448309615e0;
static double p4 = .161536412982230228262e2;
static double p3 = .26842548195503973794141e3;
static double p2 = .11530293515404850115428136e4;
static double p1 = .178040631643319697105464587e4;
static double p0 = .89678597403663861959987488e3;
static double q4 = .5895697050844462222791e2;
static double q3 = .536265374031215315104235e3;
static double q2 = .16667838148816337184521798e4;
static double q1 = .207933497444540981287275926e4;
static double q0 = .89678597403663861962481162e3;

/*
 * xatan evaluates the series, valid over [-0.414...,+0.414...].
 */
static double xatan(double arg)
{
    double argsq;
    double value;

    argsq = arg * arg;
    value = ((((p4 * argsq + p3) * argsq + p2) * argsq + p1) * argsq + p0);
    value = value / (((((argsq + q4) * argsq + q3) * argsq + q2) * argsq + q1) * argsq + q0);
    return value * arg;
}

/*
 * satan reduces its argument (known positive) to [0,0.414...] and calls xatan.
 */
static double satan(double arg)
{
    if (arg < sq2m1)
        return xatan(arg);
    else if (arg > sq2p1)
        return pio2 - xatan(1.0 / arg);
    else
        return pio4 + xatan((arg - 1.0) / (arg + 1.0));
}

/*
 * atan makes its argument positive and calls satan.
 */
double atan(double arg)
{
    if (arg > 0)
        return satan(arg);
    else
        return -satan(-arg);
}

/*
 * atan2 finds the quadrant and calls satan.
 */
double atan2(double arg1, double arg2)
{
    if ((arg1 + arg2) == arg1) {
        if (arg1 >= 0.)
            return pio2;
        else
            return -pio2;
    } else if (arg2 < 0.) {
        if (arg1 >= 0.)
            return pio2 + pio2 - satan(-arg1 / arg2);
        else
            return -pio2 - pio2 + satan(arg1 / arg2);
    } else if (arg1 > 0)
        return satan(arg1 / arg2);
    else
        return -satan(-arg1 / arg2);
}
