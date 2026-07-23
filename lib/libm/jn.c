// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// jn(n, x), yn(n, x) -- Bessel functions of integer order.
// v7 extensions, not C11; kept because v7's libm has them.
//
// v7's, unchanged but for prototypes.  Both stand entirely on j0/j1 (and y0/y1) and a
// recurrence, so all the range and precision work is next door in j0.c and j1.c;
// nothing here touches a coefficient or a threshold.  jn recurs forward for n < x and
// backward (a continued fraction, then Miller's downward recurrence) for n > x, where
// forward recurrence would be unstable; yn is stable forward for every n.  The only
// error case is yn of a non-positive x, which j0.c's y0 also reports: EDOM, -HUGE_VAL.
//
#include <errno.h>
#include <math.h>

double jn(int n, double x)
{
    int i;
    double a, b, temp;
    double xsq, t;

    if (n < 0) {
        n = -n;
        x = -x;
    }
    if (n == 0)
        return j0(x);
    if (n == 1)
        return j1(x);
    if (x == 0.)
        return 0.;
    if (n > x)
        goto recurs;

    a = j0(x);
    b = j1(x);
    for (i = 1; i < n; i++) {
        temp = b;
        b    = (2. * i / x) * b - a;
        a    = temp;
    }
    return b;

recurs:
    xsq = x * x;
    for (t = 0, i = n + 16; i > n; i--) {
        t = xsq / (2. * i - t);
    }
    t = x / (2. * n - t);

    a = t;
    b = 1;
    for (i = n - 1; i > 0; i--) {
        temp = b;
        b    = (2. * i / x) * b - a;
        a    = temp;
    }
    return t * j0(x) / b;
}

double yn(int n, double x)
{
    int i;
    int sign;
    double a, b, temp;

    if (x <= 0) {
        errno = EDOM;
        return -HUGE_VAL;
    }
    sign = 1;
    if (n < 0) {
        n = -n;
        if (n % 2 == 1)
            sign = -1;
    }
    if (n == 0)
        return y0(x);
    if (n == 1)
        return sign * y1(x);

    a = y0(x);
    b = y1(x);
    for (i = 1; i < n; i++) {
        temp = b;
        b    = (2. * i / x) * b - a;
        a    = temp;
    }
    return sign * b;
}
