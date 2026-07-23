// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// ascii to floating.
//
// v7's algorithm: accumulate the digits as an integer-valued double, count the
// decimal exponent, then scale by 10^n as 5^n (by repeated squaring) times 2^n (by
// ldexp).  v7's exponent variable is spelled `dexp' here, because <math.h> declares
// exp() and the front end has one namespace for both.
//
// The three digit loops are spelled `while (isdigit(c = *p++))' where v7 writes
// `while ((c = *p++), isdigit(c))'.  The two say the same thing; this one says it
// without the comma operator, which the front end got backwards until recently --
// it evaluated such an expression to its LEFT operand and never evaluated the right
// at all, so the loop ran while the character was non-zero and read "3.5" as the
// three digits 3, -2 and 5.  That is fixed, and the spelling stays: it is shorter,
// and it does not depend on the operator being right.
//
// Two constants are the machine's rather than the PDP-11's:
//
//   `big' is 2^40, not v7's 2^56.  It is the point past which another digit adds
//   nothing, and that is the width of the mantissa -- 40 bits here, 56 on the
//   PDP-11's double.  Set too high, the accumulator would go on multiplying by ten
//   long after the low digits had stopped surviving, and each of those multiplies
//   would round.  Digits past `big' are counted into the exponent instead, which is
//   exactly what they are worth.
//
//   LOGHUGE is 19 here and 39 there, from <math.h>: the largest finite value is
//   ~9.22e18, so anything scaled below 10^-19 is zero and there is no denormal to
//   fall back on.
//
#include <ctype.h>
#include <math.h>
#include <stdlib.h>

// 2^40 -- one more digit past this changes nothing in a 40-bit mantissa.
static double big = 1099511627776.;

double atof(const char *nptr)
{
    const char *p;
    int c;
    double fl, flexp, exp5;
    int nd;
    int eexp, dexp, neg, negexp, bexp;

    p   = nptr;
    neg = 1;
    while ((c = *p++) == ' ')
        ;
    if (c == '-')
        neg = -1;
    else if (c == '+')
        ;
    else
        --p;

    dexp = 0;
    fl   = 0;
    nd   = 0;
    while (isdigit(c = *p++)) {
        if (fl < big)
            fl = 10 * fl + (c - '0');
        else
            dexp++;
        nd++;
    }

    if (c == '.') {
        while (isdigit(c = *p++)) {
            if (fl < big) {
                fl = 10 * fl + (c - '0');
                dexp--;
            }
            nd++;
        }
    }

    negexp = 1;
    eexp   = 0;
    if ((c == 'E') || (c == 'e')) {
        if ((c = *p++) == '+')
            ;
        else if (c == '-')
            negexp = -1;
        else
            --p;

        while (isdigit(c = *p++)) {
            eexp = 10 * eexp + (c - '0');
        }
        if (negexp < 0)
            eexp = -eexp;
        dexp = dexp + eexp;
    }

    negexp = 1;
    if (dexp < 0) {
        negexp = -1;
        dexp   = -dexp;
    }

    if ((nd + dexp * negexp) < -LOGHUGE) {
        fl   = 0;
        dexp = 0;
    }
    flexp = 1;
    exp5  = 5;
    bexp  = dexp;
    for (;;) {
        if (dexp & 01)
            flexp *= exp5;
        dexp >>= 1;
        if (dexp == 0)
            break;
        exp5 *= exp5;
    }
    if (negexp < 0)
        fl /= flexp;
    else
        fl *= flexp;
    fl = ldexp(fl, negexp * bexp);
    if (neg < 0)
        fl = -fl;
    return fl;
}
