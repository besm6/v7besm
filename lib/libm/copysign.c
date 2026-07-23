//
// copysign(x, y) -- the magnitude of x with the sign of y (C11 §7.12.11.1).
//
// No v7 ancestor.  Everywhere else this is a bit graft: clear x's sign bit and OR in
// y's.  Here it cannot be, and does not need to be.  The mantissa is two's
// complement, so the sign is not a separable bit -- negating a number rewrites the
// whole mantissa -- and there is no negative zero, so `y < 0' answers the question
// completely.  On an IEEE machine it would not: copysign(1, -0.0) must be -1 there,
// and the comparison would say otherwise.
//
#include <math.h>

double copysign(double x, double y)
{
    if (x < 0.)
        x = -x;
    return y < 0. ? -x : x;
}
