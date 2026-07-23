/*
 * matht -- libm of phase 7, run under b6sim.
 *
 * libm is the v7 math library refitted to this machine's one floating format: a
 * 40-bit mantissa and a 2^+-63 exponent, with no infinities, NaNs or denormals.  Two
 * things are worth proving, and neither is a table of digits:
 *
 *   THE FUNCTIONS SATISFY THEIR IDENTITIES.  Rather than pin sin(0.7) to twelve
 *   printed digits -- which would pin the last-bit rounding of a polynomial and break
 *   on a codegen change that is not a bug -- each function is checked against another:
 *   sin^2 + cos^2 = 1, exp(log x) = x, tanh = sinh/cosh, the Bessel recurrence, and so
 *   on.  A real error moves these by orders of magnitude; a rounding difference does
 *   not move them past the tolerance.  So the .expected file is a list of `ok' lines
 *   and carries no host-computed number at all.
 *
 *   THE RANGE CHECKS DO NOT TRAP.  This is the port's real content.  On this machine
 *   an exponent that overflows 2^63 is a FAULT, not an infinity, so a guard that is a
 *   hair too loose does not return HUGE_VAL -- it ends the program with `arithmetic
 *   overflow'.  Every routine that can reach the top of the range is driven just below
 *   its limit (finite) and just past it (HUGE_VAL with ERANGE), and the domain errors
 *   (EDOM) are exercised too.  If any guard is wrong this program does not print a
 *   wrong answer; it dies, and the missing `ok' lines say where.
 *
 * fmod and fma get exact checks, because they promise exactness: fmod against the true
 * remainder on values a naive x - y*trunc(x/y) would get wrong, and fma against a
 * product whose low half a bare x*y + z would have rounded away.
 */
#include <errno.h>
#include <math.h>
#include <stdio.h>

static int errors;

static void ok(const char *name)
{
    printf("ok %s\n", name);
}

/* Relative check: got within ~1e-9 of want, which a 40-bit (~1e-12) mantissa clears
 * comfortably for a correct result and misses by orders of magnitude for a wrong one. */
static void chk(const char *name, double got, double want)
{
    double d = got - want;
    double a = want;

    if (d < 0)
        d = -d;
    if (a < 0)
        a = -a;
    if (d <= 1e-9 * a + 1e-11)
        ok(name);
    else {
        printf("FAIL %s: got %.12g want %.12g\n", name, got, want);
        errors++;
    }
}

/* Exact check: no tolerance at all. */
static void chkeq(const char *name, double got, double want)
{
    if (got == want)
        ok(name);
    else {
        printf("FAIL %s: got %.12g want %.12g\n", name, got, want);
        errors++;
    }
}

/* A routine that returned `got' must have set errno to `e' (and returned `want'). */
static void chkerrno(const char *name, double got, double want, int e)
{
    if (got == want && errno == e)
        ok(name);
    else {
        printf("FAIL %s: got %.12g/errno %d want %.12g/errno %d\n", name, got, errno, want, e);
        errors++;
    }
}

static double pi = 3.14159265358979;

static void identities(void)
{
    double s, c;

    s = sin(0.7);
    c = cos(0.7);
    chk("sin2+cos2", s * s + c * c, 1.0);
    chk("tan=sin/cos", tan(0.7), s / c);

    chk("exp(log)", exp(log(3.0)), 3.0);
    chk("log(exp)", log(exp(2.0)), 2.0);
    chk("log10(1000)", log10(1000.0), 3.0);
    chk("sqrt^2", sqrt(2.0) * sqrt(2.0), 2.0);
    chk("hypot(3,4)", hypot(3.0, 4.0), 5.0);

    chk("asin(sin)", asin(sin(0.5)), 0.5);
    chk("acos(cos)", acos(cos(0.5)), 0.5);
    chk("atan(tan)", atan(tan(0.5)), 0.5);

    /* atan2 in all four quadrants against the quarter-turns it should name. */
    chk("atan2 ++", atan2(1.0, 1.0), pi / 4);
    chk("atan2 +-", atan2(1.0, -1.0), 3 * pi / 4);
    chk("atan2 --", atan2(-1.0, -1.0), -3 * pi / 4);
    chk("atan2 -+", atan2(-1.0, 1.0), -pi / 4);

    /* pow against its own definition, integer and fractional exponent.  Not exact:
     * pow is exp(y*log x), so even an integer result carries the pair's rounding. */
    chk("pow(2,10)", pow(2.0, 10.0), 1024.0);
    chk("pow(9,.5)", pow(9.0, 0.5), 3.0);
    chk("pow=exp(y logx)", pow(3.0, 2.5), exp(2.5 * log(3.0)));
    chk("pow(-2,3)", pow(-2.0, 3.0), -8.0);

    chk("tanh=sinh/cosh", tanh(0.6), sinh(0.6) / cosh(0.6));
    chk("cosh2-sinh2", cosh(1.3) * cosh(1.3) - sinh(1.3) * sinh(1.3), 1.0);

    chk("erf+erfc", erf(0.5) + erfc(0.5), 1.0);
    chk("erf(-x)", erf(-0.8), -erf(0.8));
}

static void bessel(void)
{
    double x = 3.5;

    /* jn/yn delegate to j0/j1 for order 0 and 1. */
    chkeq("jn0=j0", jn(0, x), j0(x));
    chkeq("jn1=j1", jn(1, x), j1(x));

    /* The three-term recurrence J(n+1) = (2n/x)J(n) - J(n-1), the relation every
     * Bessel routine must satisfy, checked for J and for Y. */
    chk("jn recur", jn(2, x), (2.0 / x) * j1(x) - j0(x));
    chk("jn recur3", jn(3, x), (4.0 / x) * jn(2, x) - j1(x));
    chk("yn recur", yn(2, x), (2.0 / x) * y1(x) - y0(x));
}

static void rounding(void)
{
    chkeq("fabs", fabs(-2.5), 2.5);
    chkeq("copysign -", copysign(3.0, -1.0), -3.0);
    chkeq("copysign +", copysign(-3.0, 1.0), 3.0);
    chkeq("fmin", fmin(2.0, 5.0), 2.0);
    chkeq("fmax", fmax(2.0, 5.0), 5.0);

    chkeq("floor 2.5", floor(2.5), 2.0);
    chkeq("floor -2.5", floor(-2.5), -3.0);
    chkeq("ceil 2.5", ceil(2.5), 3.0);
    chkeq("ceil -2.5", ceil(-2.5), -2.0);
    chkeq("trunc 2.7", trunc(2.7), 2.0);
    chkeq("trunc -2.7", trunc(-2.7), -2.0);
    chkeq("round 2.5", round(2.5), 3.0);
    chkeq("round -2.5", round(-2.5), -3.0);
    chkeq("round 2.4", round(2.4), 2.0);
    chkeq("round -2.4", round(-2.4), -2.0);
}

static void exact(void)
{
    double big;

    /* fmod on small exact values: sign of x, magnitude the true remainder. */
    chkeq("fmod 7,3", fmod(7.0, 3.0), 1.0);
    chkeq("fmod -7,3", fmod(-7.0, 3.0), -1.0);
    chkeq("fmod 7.5,2", fmod(7.5, 2.0), 1.5);

    /*
     * fmod where x - y*trunc(x/y) loses everything: 2^42 is exactly representable,
     * but 2^42/3 is above 2^40 and cannot be, so the naive quotient drops its low
     * digits and the naive remainder is wrong.  The scale-and-subtract loop keeps
     * every bit.  2^42 = (2^2)^21 == 1 (mod 3), and (mod 5) 2^42 = 2^40*4 == 4.
     */
    big = ldexp(1.0, 42); /* 2^42, exact */
    chkeq("fmod 2^42,3", fmod(big, 3.0), 1.0);
    chkeq("fmod 2^42,5", fmod(big, 5.0), 4.0);

    /* fma is exact: fma(2,3,4) has no rounding to lose, so it is 10 outright. */
    chkeq("fma exact", fma(2.0, 3.0, 4.0), 10.0);

    /*
     * fma keeps the product's low half.  x = 1 + 2^-20 is exact; x*x rounds and drops
     * a bit near 2^-40, which the following subtraction of 1 would otherwise lose for
     * good.  fma carries it, so fma(x,x,-1) and the naive x*x - 1 must DIFFER -- that
     * difference is the bit the routine exists to preserve.
     */
    {
        double x = 1.0 + ldexp(1.0, -20);
        double rf = fma(x, x, -1.0);
        double rn = x * x - 1.0;

        chk("fma value", rf, 2.0 * ldexp(1.0, -20) + ldexp(1.0, -40));
        if (rf != rn)
            ok("fma keeps bit");
        else {
            printf("FAIL fma keeps bit: fma and naive both %.14g\n", rf);
            errors++;
        }
    }
}

static void ranges(void)
{
    double v;

    /* exp: finite just below the limit, ERANGE + HUGE_VAL past it, zero far below.
     * None of these may fault. */
    errno = 0;
    v     = exp(43.5);
    chkerrno("exp finite", v > 0.0 && v < HUGE_VAL ? v : -1.0, v, 0);
    errno = 0;
    v     = exp(50.0);
    chkerrno("exp overflow", v, HUGE_VAL, ERANGE);
    errno = 0;
    v     = exp(-50.0);
    chkerrno("exp underflow", v, 0.0, 0);

    /* sinh/cosh reach the same top through exp(arg - ln2): finite at 14, ERANGE past
     * where the halved exponential overflows. */
    errno = 0;
    v     = sinh(14.0);
    chkerrno("sinh finite", v > 0.0 && v < HUGE_VAL ? v : -1.0, v, 0);
    errno = 0;
    v     = sinh(50.0);
    chkerrno("sinh overflow", v, HUGE_VAL, ERANGE);
    errno = 0;
    v     = cosh(50.0);
    chkerrno("cosh overflow", v, HUGE_VAL, ERANGE);

    /* Domain errors: log of non-positive, sqrt/asin/acos out of domain, fmod by zero,
     * pow(0,0), and the Bessel Y's at non-positive x. */
    errno = 0;
    chkerrno("log 0", log(0.0), -HUGE_VAL, EDOM);
    errno = 0;
    chkerrno("log -1", log(-1.0), -HUGE_VAL, EDOM);
    errno = 0;
    chkerrno("sqrt -1", sqrt(-1.0), 0.0, EDOM);
    errno = 0;
    chkerrno("asin 2", asin(2.0), 0.0, EDOM);
    errno = 0;
    chkerrno("acos 2", acos(2.0), 0.0, EDOM);
    errno = 0;
    chkerrno("fmod x,0", fmod(5.0, 0.0), 0.0, EDOM);
    errno = 0;
    chkerrno("pow 0,0", pow(0.0, 0.0), 0.0, EDOM);
    errno = 0;
    chkerrno("y0 0", y0(0.0), -HUGE_VAL, EDOM);
    errno = 0;
    chkerrno("yn 2,-1", yn(2, -1.0), -HUGE_VAL, EDOM);
}

int main(void)
{
    identities();
    bessel();
    rounding();
    exact();
    ranges();

    printf("%d error(s)\n", errors);
    return errors != 0;
}
