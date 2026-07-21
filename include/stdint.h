/*
 * <stdint.h> — integer types of specified widths (C11 §7.20), BESM-6 target.
 *
 * BESM-6 has no native 16/32/64-bit storage: every integer occupies one 48-bit
 * word (signed 41-bit, unsigned 48-bit).  Exact-width types are optional in
 * C11; we provide only the ones that genuinely exist:
 *
 *   - int8_t / uint8_t  = signed char / unsigned char (true 8-bit bytes).
 *   - NO int16_t / int32_t / int64_t : no type has exactly those widths.
 *   - least/fast types map to int/unsigned (the word holds 41/48 bits).
 *   - NO 64-bit least/fast types: the widest signed value is 41-bit.
 *
 * Consequently intmax_t is only 41-bit signed and uintmax_t 48-bit unsigned —
 * this is the integer ceiling of the machine.
 */
#ifndef _STDINT_H
#define _STDINT_H

/* Exact-width (only the 8-bit byte exists). */
typedef signed char   int8_t;
typedef unsigned char uint8_t;

/* Minimum-width: smallest type holding at least N bits.  One word covers all. */
typedef signed char   int_least8_t;
typedef unsigned char uint_least8_t;
typedef int           int_least16_t;
typedef unsigned int  uint_least16_t;
typedef int           int_least32_t;
typedef unsigned int  uint_least32_t;

/* Fastest type holding at least N bits — the native word is fastest. */
typedef int           int_fast8_t;
typedef unsigned int  uint_fast8_t;
typedef int           int_fast16_t;
typedef unsigned int  uint_fast16_t;
typedef int           int_fast32_t;
typedef unsigned int  uint_fast32_t;

/* Pointer-sized: a pointer holds a 15-bit word address, fits in one word. */
typedef int          intptr_t;
typedef unsigned int uintptr_t;

/* Greatest-width: bounded by the machine word. */
typedef long long          intmax_t;
typedef unsigned long long uintmax_t;

/* --- limits --- */
#define INT8_MIN  (-128)
#define INT8_MAX  127
#define UINT8_MAX 255

#define INT_LEAST8_MIN   (-128)
#define INT_LEAST8_MAX   127
#define UINT_LEAST8_MAX  255
#define INT_LEAST16_MIN  (-1099511627776L)
#define INT_LEAST16_MAX  1099511627775L
#define UINT_LEAST16_MAX 281474976710655UL
#define INT_LEAST32_MIN  (-1099511627776L)
#define INT_LEAST32_MAX  1099511627775L
#define UINT_LEAST32_MAX 281474976710655UL

#define INT_FAST8_MIN   (-1099511627776L)
#define INT_FAST8_MAX   1099511627775L
#define UINT_FAST8_MAX  281474976710655UL
#define INT_FAST16_MIN  (-1099511627776L)
#define INT_FAST16_MAX  1099511627775L
#define UINT_FAST16_MAX 281474976710655UL
#define INT_FAST32_MIN  (-1099511627776L)
#define INT_FAST32_MAX  1099511627775L
#define UINT_FAST32_MAX 281474976710655UL

#define INTPTR_MIN  (-1099511627776L)
#define INTPTR_MAX  1099511627775L
#define UINTPTR_MAX 281474976710655UL

#define INTMAX_MIN  (-1099511627776L)
#define INTMAX_MAX  1099511627775L
#define UINTMAX_MAX 281474976710655UL

#define PTRDIFF_MIN (-1099511627776L)
#define PTRDIFF_MAX 1099511627775L
#define SIZE_MAX    281474976710655UL

#define WCHAR_MIN (-1099511627776L)
#define WCHAR_MAX 1099511627775L
#define WINT_MIN  (-1099511627776L)
#define WINT_MAX  1099511627775L

/* --- constant-expression macros --- */
#define INT8_C(v)   (v)
#define UINT8_C(v)  (v##U)
#define INT16_C(v)  (v##L)
#define UINT16_C(v) (v##UL)
#define INT32_C(v)  (v##L)
#define UINT32_C(v) (v##UL)
#define INTMAX_C(v)  (v##LL)
#define UINTMAX_C(v) (v##ULL)

#endif /* _STDINT_H */
