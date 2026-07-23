/*
 * Taken from the c-compiler's libc/besm6/modf.c, unchanged: it is already written
 * for this machine's floating format.  The printf engine (stdio/doprnt.c), ecvt()
 * and atof() are what need it, which is why it is in libc and not in the libm of
 * lib phase 7.
 *
 * modf() splits x into its integral and fractional parts, both with the sign of x.
 * Any magnitude at or above 2^40 is already integral in a 40-bit mantissa, so the
 * fractional part is zero; below that the integer part fits in a 41-bit `int', so a
 * truncating round trip through int recovers it exactly.
 */
#include <math.h>

/* 2^40 -- the smallest magnitude with no fractional bits left in the mantissa. */
static double two40 = 1099511627776.0;

double modf(double x, double *iptr)
{
    double a, ip;

    if (x < 0) {
        a = -x;
        if (a >= two40)
            ip = x;
        else
            ip = -(double)(int)a;
    } else {
        if (x >= two40)
            ip = x;
        else
            ip = (double)(int)x;
    }
    *iptr = ip;
    return x - ip;
}
