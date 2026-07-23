// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// ecvt/fcvt convert to decimal.  The number of digits is given by ndigits, *decpt
// receives the position of the decimal point and *sign is 0 for positive, 1 for
// negative.  ecvt counts ndigits SIGNIFICANT digits; fcvt counts them after the
// point.  The result is a static buffer, overwritten by the next call.
//
// v7's, unchanged: it is decimal digit shuffling over modf(), and nothing in it
// assumes a width.  Neither routine is C11 -- they are v7 extensions no header
// declares, so a caller declares them itself, as gcvt() does.
//
// The `+.03' fudges are v7's and are still right: modf(fi/10) leaves a fraction
// within a few ulps of k/10, and adding 0.03 before the multiply lands it on k
// without ever reaching k+1.
//
#include <math.h>

#define NDIG 80

static char *cvt(double arg, int ndigits, int *decpt, int *sign, int eflag)
{
    int r2;
    double fi, fj;
    char *p, *p1;
    static char buf[NDIG];

    if (ndigits < 0)
        ndigits = 0;
    if (ndigits >= NDIG - 1)
        ndigits = NDIG - 2;
    r2    = 0;
    *sign = 0;
    p     = &buf[0];
    if (arg < 0) {
        *sign = 1;
        arg   = -arg;
    }
    arg = modf(arg, &fi);
    p1  = &buf[NDIG];
    //
    // Do integer part
    //
    if (fi != 0) {
        p1 = &buf[NDIG];
        while (fi != 0) {
            fj    = modf(fi / 10, &fi);
            *--p1 = (int)((fj + .03) * 10) + '0';
            r2++;
        }
        while (p1 < &buf[NDIG])
            *p++ = *p1++;
    } else if (arg > 0) {
        while ((fj = arg * 10) < 1) {
            arg = fj;
            r2--;
        }
    }
    p1 = &buf[ndigits];
    if (eflag == 0)
        p1 += r2;
    *decpt = r2;
    if (p1 < &buf[0]) {
        buf[0] = '\0';
        return buf;
    }
    while (p <= p1 && p < &buf[NDIG]) {
        arg *= 10;
        arg  = modf(arg, &fj);
        *p++ = (int)fj + '0';
    }
    if (p1 >= &buf[NDIG]) {
        buf[NDIG - 1] = '\0';
        return buf;
    }
    p = p1;
    *p1 += 5;
    while (*p1 > '9') {
        *p1 = '0';
        if (p1 > buf)
            ++*--p1;
        else {
            *p1 = '1';
            (*decpt)++;
            if (eflag == 0) {
                if (p > buf)
                    *p = '0';
                p++;
            }
        }
    }
    *p = '\0';
    return buf;
}

char *ecvt(double arg, int ndigits, int *decpt, int *sign)
{
    return cvt(arg, ndigits, decpt, sign, 1);
}

char *fcvt(double arg, int ndigits, int *decpt, int *sign)
{
    return cvt(arg, ndigits, decpt, sign, 0);
}
