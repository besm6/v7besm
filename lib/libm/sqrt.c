/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * sqrt(x) -- the square root, by Newton's method (C11 §7.12.7.5).
 *
 * v7's algorithm: split off an even power of two, seed with a linear fit of the
 * remaining fraction, then five Newton steps -- each of which doubles the correct
 * digits, so five is more than a 40-bit mantissa can use.
 *
 * Two changes.  v7 puts the exponent back on by multiplying or dividing by 1L<<30 in
 * a loop and then by 1L<<(exp/2), which is a way of saying ldexp on a machine whose
 * ldexp was not in the library; ours is, it is four instructions and it is exact, and
 * exp is even by then so exp/2 truncates nothing.  The `float sqrtf(float)' the file
 * arrived with is gone: float and double are the same one word here and <math.h>
 * declares no f-suffixed forms at all.
 *
 * `dexp' rather than v7's `exp', for the reason atof.c renames its own: the front end
 * has one namespace for objects and functions, and <math.h> has already declared
 * exp().
 */
#include <errno.h>
#include <math.h>

double sqrt(double arg)
{
    double x, temp;
    int dexp;
    int i;

    if (arg <= 0.) {
        if (arg < 0.)
            errno = EDOM;
        return 0.;
    }
    x = frexp(arg, &dexp);
    while (x < 0.5) {
        x *= 2;
        dexp--;
    }
    /*
     * Halve the exponent, which means making it even first.  v7 notes here that
     * this would not work on a ones-complement machine; this one is two's
     * complement, so dexp & 1 answers for a negative dexp as well.
     */
    if (dexp & 1) {
        x *= 2;
        dexp--;
    }
    temp = 0.5 * (1.0 + x);
    temp = ldexp(temp, dexp / 2);

    for (i = 0; i <= 4; i++)
        temp = 0.5 * (temp + arg / temp);
    return temp;
}
