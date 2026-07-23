//
// fmin(x, y) -- the lesser of two values (C11 §7.12.12.3).
//
// No v7 ancestor.  C11 asks fmin to treat a NaN operand as missing data and return
// the other one; there are no NaNs here, so what is left is the comparison.
//
#include <math.h>

double fmin(double x, double y)
{
    return x < y ? x : y;
}
