/*
 * <float.h> — characteristics of floating types (C11 §7.7), BESM-6 target.
 *
 * BESM-6 has a single native floating format used for float, double and long
 * double alike (one 48-bit word): a 40-bit two's-complement mantissa and a
 * 7-bit exponent biased by 64, giving binary exponents 2^-63 .. 2^63.
 * There are no infinities, NaNs, denormals or negative zero.
 * Precision is 40 mantissa bits ~= 12 decimal digits.
 */
#ifndef _FLOAT_H
#define _FLOAT_H

#define FLT_RADIX       2
#define FLT_ROUNDS      1 /* round to nearest */
#define FLT_EVAL_METHOD 0 /* evaluate in the operands' own type */
#define DECIMAL_DIG     12

/* float == double == long double on BESM-6. */
#define FLT_MANT_DIG 40
#define DBL_MANT_DIG 40
#define LDBL_MANT_DIG 40

#define FLT_DIG  12
#define DBL_DIG  12
#define LDBL_DIG 12

/* Binary exponent range of the normalized mantissa in [0.5,1). */
#define FLT_MIN_EXP  (-64)
#define DBL_MIN_EXP  (-64)
#define LDBL_MIN_EXP (-64)
#define FLT_MAX_EXP  63
#define DBL_MAX_EXP  63
#define LDBL_MAX_EXP 63

/* Decimal exponent range, floor/ceil of the binary range * log10(2). */
#define FLT_MIN_10_EXP  (-19)
#define DBL_MIN_10_EXP  (-19)
#define LDBL_MIN_10_EXP (-19)
#define FLT_MAX_10_EXP  18
#define DBL_MAX_10_EXP  18
#define LDBL_MAX_10_EXP 18

/* eps = 2^-(MANT_DIG-1) = 2^-39. */
#define FLT_EPSILON  1.8189894035458565e-12
#define DBL_EPSILON  1.8189894035458565e-12
#define LDBL_EPSILON 1.8189894035458565e-12

/* Smallest positive normalized value = 0.5 * 2^-63 = 2^-64. */
#define FLT_MIN  5.421010862427522e-20
#define DBL_MIN  5.421010862427522e-20
#define LDBL_MIN 5.421010862427522e-20

/* Largest finite value = (1 - 2^-40) * 2^63. */
#define FLT_MAX  9.223372036846553e+18
#define DBL_MAX  9.223372036846553e+18
#define LDBL_MAX 9.223372036846553e+18

#endif /* _FLOAT_H */
