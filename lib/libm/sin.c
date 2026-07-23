// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// sin(x), cos(x) -- the circular sine and cosine (C11 §7.12.4.6, §7.12.4.5).
//
// v7's rational fit (Hart & Cheney #3370, 18.80D).  There are no error returns: the
// argument is reduced modulo a quarter-turn and the fit runs on the reduced value.
//
// THE ONE CHANGE is the crossover from the integer reduction to the floating one.
// v7 writes `if (x > 32764)', which is not a mathematical bound but the last multiple
// of the quarter-turn below 2^15 -- the PDP-11's `int' overflowed past it, so above it
// v7 falls back on a slower reduction through modf.  Here `int' is 41 bits, and the
// real point past which `k = x' loses bits is 2^40, the width of the mantissa -- the
// same two40 that modf() keys on (lib/libc/gen/modf.c).  So the crossover moves out to
// two40 and the fast path serves the whole range a program is likely to ask for.
// (A magnitude past 2^40 has no fractional turn left to speak of anyway, so the
// floating reduction above it is exact where it matters and meaningless where it is
// not.)
//
#include <math.h>

static double two40 = 1099511627776.0; // 2^40: no fractional bits survive above it

static double twoopi = 0.63661977236758134308;
static double p0 = .1357884097877375669092680e8;
static double p1 = -.4942908100902844161158627e7;
static double p2 = .4401030535375266501944918e6;
static double p3 = -.1384727249982452873054457e5;
static double p4 = .1459688406665768722226959e3;
static double q0 = .8644558652922534429915149e7;
static double q1 = .4081792252343299749395779e6;
static double q2 = .9463096101538208180571257e4;
static double q3 = .1326534908786136358911494e3;

static double sinus(double arg, int quad)
{
    double e, f;
    double ysq;
    double x, y;
    int k;
    double temp1, temp2;

    x = arg;
    if (x < 0) {
        x    = -x;
        quad = quad + 2;
    }
    x = x * twoopi; // underflow?
    if (x > two40) {
        y = modf(x, &e);
        e = e + quad;
        modf(0.25 * e, &f);
        quad = e - 4 * f;
    } else {
        k    = x;
        y    = x - k;
        quad = (quad + k) & 03;
    }
    if (quad & 01)
        y = 1 - y;
    if (quad > 1)
        y = -y;

    ysq   = y * y;
    temp1 = ((((p4 * ysq + p3) * ysq + p2) * ysq + p1) * ysq + p0) * y;
    temp2 = ((((ysq + q3) * ysq + q2) * ysq + q1) * ysq + q0);
    return temp1 / temp2;
}

double cos(double arg)
{
    if (arg < 0)
        arg = -arg;
    return sinus(arg, 1);
}

double sin(double arg)
{
    return sinus(arg, 0);
}
