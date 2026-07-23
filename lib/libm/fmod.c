//
// fmod(x, y) -- the remainder of x/y, with the sign of x and no rounding error
// (C11 §7.12.10.1).
//
// Written here rather than ported.  The fmod.c this tree inherited is not v7's --
// v7's libm had no fmod at all -- but an fdlibm-derived import that takes an IEEE
// binary32 or binary64 apart through a union of uint32_t halves and reassembles it.
// Every line of it is about a format this machine does not have: there is no 32-bit
// word to alias a double onto, no sign-magnitude sign bit, no biased-by-1023
// exponent field at bit 20 of a high half, and no subnormals to renormalize.  So it
// is replaced by the classic scale-and-subtract loop, which needs no bit access at
// all -- only frexp and ldexp, both of which are exact here.
//
// The loop is exact for the reason Sterbenz gives: if t <= a <= 2t then a - t is
// representable with no rounding whatever.  Each step chooses t = y * 2^n as the
// largest such multiple not exceeding a, so no bit is ever lost and the answer is
// the true remainder and not an approximation to it.  This is what x - y*trunc(x/y)
// cannot do: that form has already lost the low bits of the quotient before the
// multiply, and for x/y much above 2^40 it has lost all of them.
//
// It terminates in at most 127 steps, one per exponent: after a step a < t, and t is
// always y times a power of two, so the next t is at most half this one.
//
#include <errno.h>
#include <math.h>

double fmod(double x, double y)
{
    double a, b, t;
    int ex, ey;

    if (y == 0.) {
        errno = EDOM;
        return 0.;
    }

    a = fabs(x);
    b = fabs(y);
    while (a >= b) {
        //
        // Only the exponents are wanted; frexp's fraction is what makes the two
        // comparable.  a and b are f * 2^e with f in [0.5,1), so b * 2^(ex-ey) has
        // a's exponent exactly, and is at most twice a -- one halving is all the
        // correction that can ever be needed.
        //
        frexp(a, &ex);
        frexp(b, &ey);
        t = ldexp(b, ex - ey);
        if (t > a)
            t = ldexp(b, ex - ey - 1);
        a -= t;
    }

    return x < 0. ? -a : a;
}
