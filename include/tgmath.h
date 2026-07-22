// <tgmath.h> -- type-generic math (C11 §7.25), BESM-6 target.
//
// Adapted from the c-compiler's libc/besm6/include/tgmath.h.
//
// Type-generic dispatch is degenerate here and the header collapses to almost
// nothing.  float, double and long double are the same one-word native format,
// so a _Generic selecting among them would name the identical <math.h> function
// in every association; and there is no <complex.h> (__STDC_NO_COMPLEX__ is
// defined -- see cmd/cpp/cpp.c), so the complex half of §7.25 has nothing to
// dispatch to at all.  What is left is: promote the argument to double and call
// the one real function.  Integer arguments promote to double exactly as §7.25p2
// requires.
//
// Each macro is self-referential.  Per the no-recursive-expansion rule of
// §6.10.3.4 the inner name is not re-examined, so it resolves to the <math.h>
// function and not back to the macro -- the same "blue paint" behaviour the
// b6cpp conformance suite covers.  It is also why nothing here needs a
// _Generic: a macro that expands to a cast plus itself is the whole mechanism.
#ifndef _TGMATH_H
#define _TGMATH_H

#include <math.h>

// unary real functions
#define fabs(x)  fabs((double)(x))
#define floor(x) floor((double)(x))
#define ceil(x)  ceil((double)(x))
#define round(x) round((double)(x))
#define trunc(x) trunc((double)(x))
#define sqrt(x)  sqrt((double)(x))
#define exp(x)   exp((double)(x))
#define log(x)   log((double)(x))
#define log10(x) log10((double)(x))
#define sin(x)   sin((double)(x))
#define cos(x)   cos((double)(x))
#define tan(x)   tan((double)(x))
#define asin(x)  asin((double)(x))
#define acos(x)  acos((double)(x))
#define atan(x)  atan((double)(x))
#define sinh(x)  sinh((double)(x))
#define cosh(x)  cosh((double)(x))
#define tanh(x)  tanh((double)(x))

// binary real functions
#define pow(x, y)      pow((double)(x), (double)(y))
#define atan2(y, x)    atan2((double)(y), (double)(x))
#define fmod(x, y)     fmod((double)(x), (double)(y))
#define hypot(x, y)    hypot((double)(x), (double)(y))
#define fmin(x, y)     fmin((double)(x), (double)(y))
#define fmax(x, y)     fmax((double)(x), (double)(y))
#define copysign(x, y) copysign((double)(x), (double)(y))

// mixed-argument functions: only the real argument is type-generic
#define frexp(x, ep) frexp((double)(x), (ep))
#define ldexp(x, n)  ldexp((double)(x), (n))
#define modf(x, ip)  modf((double)(x), (ip))

#endif // _TGMATH_H
