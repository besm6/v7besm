//
// round(x) -- the nearest integer to x, halfway cases away from zero (C11 §7.12.9.6).
//
// No v7 ancestor.  Written over modf() rather than as floor(x + 0.5), which is the
// usual shortcut and is wrong twice: it rounds halfway cases toward +infinity rather
// than away from zero, and for an x just below 0.5 the addition itself rounds up to
// 1.0 and the answer comes back 1 instead of 0.
//
// modf() gives both parts the sign of x, so the step away from zero is a step in the
// direction of ip and needs no separate sign test beyond the one below.  Nothing can
// overflow: |fract| < 1, so ip moves by at most one and any x large enough for that
// to matter had a zero fraction to begin with.
//
#include <math.h>

double round(double x)
{
    double fract, ip;

    fract = modf(x, &ip);
    if (fract >= 0.5)
        ip += 1.0;
    else if (fract <= -0.5)
        ip -= 1.0;
    return ip;
}
