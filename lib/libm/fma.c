/*
 * fma(x, y, z) -- x*y + z with the product formed exactly (C11 §7.12.13.1).
 *
 * No v7 ancestor, and no wider type to compute in: float, double and long double
 * are the same one 48-bit word.  What makes the routine possible anyway is that the
 * mantissa is 40 bits and 40 halves evenly.  Split each operand into two 20-bit
 * pieces and all four cross products are 20x20 = 40-bit values, every one of them
 * exactly representable -- so the product x*y is recovered as a sum p + e of two
 * machine numbers with nothing dropped, and z is added to that rather than to a
 * product whose low half has already been rounded away.  It is the whole point of
 * the routine: a caller who wanted x*y + z could have written x*y + z.
 *
 * THE SPLIT IS NOT DEKKER'S.  The textbook split multiplies by 2^20 + 1 and relies
 * on the multiply rounding to nearest to place the boundary; this machine rounds by
 * forcing the low mantissa bit when anything is discarded (see the round_flag arm of
 * Processor::arith_normalize_and_round in cmd/sim/arithmetic.cpp), which is faithful
 * but not nearest, and the theorem does not survive it.  The split below assumes
 * nothing about rounding at all: ldexp moves the binary point, modf cuts at it, and
 * both are exact.  With the operand scaled into [0.5,1) its mantissa is an integer
 * multiple of 2^-40, so the cut at 2^-20 puts 20 bits either side by construction,
 * and xl = x - xh is exact by Sterbenz (x/2 <= xh <= x).
 *
 * The operands are scaled into [0.5,1) rather than split where they lie, because the
 * split shifts left by 20 and an unscaled operand above 2^43 would run off the top of
 * the exponent range -- and running off the top is a fault here, not an infinity.
 * The exponents go back on afterwards with ldexp, which is exact.  If the product
 * itself overflows, that ldexp faults, which is what the bare x*y would have done and
 * what the caller asked for.
 *
 * WHAT IS AND IS NOT GUARANTEED.  The product is exact: p + e is x*y with no error
 * term left over.  The residual accumulation and the closing sum are faithful rather
 * than provably single-rounded, for the same reason the split could not be Dekker's.
 * The property that matters -- that the low half of the product reaches the addition
 * instead of being rounded away before it -- holds outright, and that is what lib/
 * test/matht.c pins.
 */
#include <math.h>

/*
 * Split f, known to lie in [0.5,1), at 2^-20: *hi takes the top 20 mantissa bits and
 * the return value the bottom 20.  Both are exact and neither rounds.
 */
static double split(double f, double *hi)
{
    double ip;

    modf(ldexp(f, 20), &ip);
    *hi = ldexp(ip, -20);
    return f - *hi;
}

double fma(double x, double y, double z)
{
    double xh, xl, yh, yl, p, e, s, b, err;
    int ex, ey;

    /* A zero factor leaves z untouched, and there is nothing to round twice. */
    if (x == 0. || y == 0.)
        return z;

    x = frexp(x, &ex);
    y = frexp(y, &ey);

    xl = split(x, &xh);
    yl = split(y, &yh);

    /* p is the rounded product; e is everything p had to drop. */
    p = x * y;
    e = ((xh * yh - p) + xh * yl + xl * yh) + xl * yl;

    /*
     * Back to the true scale.  e can flush to zero here where p cannot, and that is
     * harmless: it means the residual itself fell below 2^-63, forty bits under a
     * product that did not.
     */
    p = ldexp(p, ex + ey);
    e = ldexp(e, ex + ey);

    /* s + err is p + z, carrying the bits the addition alone would have dropped. */
    s   = p + z;
    b   = s - p;
    err = (p - (s - b)) + (z - b);

    return s + (err + e);
}
