/*
 * <stddef.h> — common definitions (C11 §7.19), BESM-6 target.
 *
 * Freestanding header: types and macros only, no runtime.  Every BESM-6 scalar
 * occupies one 48-bit word; pointers hold a 15-bit word address.
 */
#ifndef _STDDEF_H
#define _STDDEF_H

/* ptrdiff_t: result of subtracting two pointers.  Signed, one word (41-bit). */
typedef long ptrdiff_t;

/* size_t: result of sizeof.  Unsigned, one word (48-bit). */
typedef unsigned long size_t;

/* wchar_t: wide character.  One word; holds any BESM-6/KOI7 code point. */
typedef int wchar_t;

/*
 * max_align_t: a type whose alignment is the strictest the implementation has.
 * On the word-addressed BESM-6 every type is 1-word aligned, so any scalar will
 * do; use double for parity with hosted toolchains.
 */
typedef double max_align_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

/* Byte offset of MEMBER within struct/union TYPE. */
#define offsetof(type, member) ((size_t) & (((type *)0)->member))

#endif /* _STDDEF_H */
