//
// trunc(x) -- the integer part of x, rounded toward zero (C11 §7.12.9.8).
//
// No v7 ancestor: v7's libm stopped at floor and ceil.  It is modf() with the
// fraction thrown away, which is exactly the definition -- modf splits toward zero
// and gives both parts the sign of x.
//
#include <math.h>

double trunc(double x)
{
    double ip;

    modf(x, &ip);
    return ip;
}
