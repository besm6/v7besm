/*
 * <limits.h> — sizes of integer types (C11 §7.10), BESM-6 target.
 *
 * BESM-6 integer model (see doc/Besm6_Data_Representation.md):
 *   - CHAR_BIT is 8; plain char is UNSIGNED.
 *   - Every integer occupies one 48-bit word.
 *   - Signed int/short/long/long long use a 41-bit two's-complement field
 *     (1 sign bit + 40 value bits): range -2^40 .. 2^40-1.
 *   - Unsigned variants use the full 48 bits: range 0 .. 2^48-1.
 *
 * Note: the signed minimum is -2^40 (raw two's complement, exponent field = 0).
 * The INT<->FP conversion helpers (abi.h BESM_INT_MIN) use a symmetric ±(2^40-1)
 * range, so a value rounded through floating point cannot reach INT_MIN exactly.
 */
#ifndef _LIMITS_H
#define _LIMITS_H

#define CHAR_BIT 8

/* Multibyte: up to 6 KOI7 bytes pack into one word. */
#define MB_LEN_MAX 6

#define SCHAR_MIN (-128)
#define SCHAR_MAX 127
#define UCHAR_MAX 255

/* Plain char is unsigned on BESM-6. */
#define CHAR_MIN 0
#define CHAR_MAX 255

/* Signed: 41-bit field, -2^40 .. 2^40-1. */
#define SHRT_MIN  (-1099511627776L)
#define SHRT_MAX  1099511627775L
#define INT_MIN   (-1099511627776L)
#define INT_MAX   1099511627775L
#define LONG_MIN  (-1099511627776L)
#define LONG_MAX  1099511627775L
#define LLONG_MIN (-1099511627776L)
#define LLONG_MAX 1099511627775L

/* Unsigned: full 48-bit word, 0 .. 2^48-1. */
#define USHRT_MAX  281474976710655UL
#define UINT_MAX   281474976710655UL
#define ULONG_MAX  281474976710655UL
#define ULLONG_MAX 281474976710655UL

#endif /* _LIMITS_H */
