/*
 * fmax(x, y) -- the greater of two values (C11 §7.12.12.2).
 *
 * No v7 ancestor.  See fmin.c: with no NaN to treat as missing data, the whole of
 * it is the comparison.
 */
#include <math.h>

double fmax(double x, double y)
{
    return x > y ? x : y;
}
