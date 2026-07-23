// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// <math.h> -- mathematics (C11 §7.12).
//
// Everything below is answered by lib/libm (libm.a), the v7 sources refitted to
// the 40-bit mantissa and the 2^±63 exponent, except for the three marked as
// living in libc.  A program that calls any of them links with -lm, which b6cc
// places ahead of the implicit -lc: libm calls errno, frexp, ldexp and modf, and
// libc calls nothing back.
//
// float == double == long double.  There is one native floating format, one word
// wide, so the single double-typed entry point serves every floating type and
// there are no `f'/`l' suffixed variants -- see <tgmath.h>, which collapses for
// the same reason.
//
// There are NO infinities, NaNs or denormals.  So this header defines no
// INFINITY, no NAN, no FP_* classification numbers, no fpclassify/isnan/isinf/
// isfinite/isnormal, no signbit, and no math_errhandling: none of them has
// anything to classify.  A routine that would have returned an infinity sets
// ERANGE and returns HUGE_VAL, which is the largest finite value and not a
// distinguished one.
//
// And HUGE_VAL is a value a routine RETURNS, never one it computes.  The two
// ends of the exponent range are not symmetric: an exponent that falls below
// 2^-63 quietly becomes machine zero, but one that rises past 2^63 raises an
// arithmetic-overflow fault and the program dies.  So every range check in libm
// is made BEFORE the arithmetic that would overflow, and is strict -- see
// lib/README.md.
//
// v7's HUGE was 1.701411733192644270e38 -- the PDP-11's largest float, twenty
// orders of magnitude past this machine's 9.223372036846553e18, so it could not
// even be written as a literal here.  It is kept as the alias of HUGE_VAL that
// the v7 sources expect, carrying this machine's value.  LOGHUGE is v7's
// companion, ceil(log10(HUGE)): 39 there, 19 here.
#ifndef _MATH_H
#define _MATH_H

#include <float.h>

#define HUGE_VAL  DBL_MAX
#define HUGE_VALF FLT_MAX
#define HUGE_VALL LDBL_MAX

#define HUGE    HUGE_VAL             // v7's name for it
#define LOGHUGE (DBL_MAX_10_EXP + 1) // v7's ceil(log10(HUGE)): 39 there, 19 here

// Not C11 -- v7 put them here and the v7 sources use them.
#define M_PI 3.14159265358979
#define M_E  2.71828182845905

// ---- implemented in libc.a, not libm ----
// These three are the exponent surgery the CONVERSIONS need -- ecvt(), atof() and
// the printf engine -- so they are in libc, where v7 keeps them too (its gen/ had
// modf.s, frexp.s and ldexp.s).  A program gets them without -lm.
double modf(double x, double *iptr);
double frexp(double x, int *exp);
double ldexp(double x, int exp);

// ---- implemented in libm.a ----
double fabs(double x);
double floor(double x);
double ceil(double x);
double round(double x);
double trunc(double x);
double fmod(double x, double y);

double sqrt(double x);
double pow(double x, double y);
double exp(double x);
double log(double x);
double log10(double x);

double sin(double x);
double cos(double x);
double tan(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);
double sinh(double x);
double cosh(double x);
double tanh(double x);

double hypot(double x, double y);
double copysign(double x, double y);
double fmin(double x, double y);
double fmax(double x, double y);
double fma(double x, double y, double z);

// v7 extensions, not C11: the error function and the Bessel family.  v7's
// <math.h> declared erf and erfc and ours did not, which was an oversight -- the
// sources were always there.
//
// v7's gamma() is NOT here.  It is the one routine of v7's libm whose source
// never reached this tree, nothing in the repo calls it, it is not C11 and
// <tgmath.h> does not name it either.  It comes back with the first program that
// wants it, and the choice between v7's log-gamma-plus-signgam and C11's
// tgamma/lgamma gets made then.
double erf(double x);
double erfc(double x);
double j0(double x);
double j1(double x);
double jn(int n, double x);
double y0(double x);
double y1(double x);
double yn(int n, double x);

#endif // _MATH_H
