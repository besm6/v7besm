// <uchar.h> -- Unicode utilities (C11 §7.28), BESM-6 target.
//
// Adapted from the c-compiler's libc/besm6/include/uchar.h.
//
// §7.28p2 requires char16_t to hold any UTF-16 code unit and char32_t any UTF-32
// one, so on a machine with 16- and 32-bit types they would be uint_least16_t
// and uint_least32_t.  Here both of those ARE the native word (see the note in
// <inttypes.h> about which minimum-width types exist), so both typedefs are
// `unsigned' -- 48 bits, which covers either code unit with room to spare.
//
// The conversions convert between UTF-8 and those units, not between the
// execution character set and them; with an ASCII execution set the two agree
// over 0..0177, which is the whole of it.
//
// TODO: the four routines, in libc.
#ifndef _UCHAR_H
#define _UCHAR_H

#include <stddef.h> // size_t
#include <wchar.h>  // mbstate_t

typedef unsigned char16_t;
typedef unsigned char32_t;

// ---- declared for future implementation (TODO) ----
size_t mbrtoc16(char16_t *pc16, const char *s, size_t n, mbstate_t *ps);
size_t c16rtomb(char *s, char16_t c16, mbstate_t *ps);
size_t mbrtoc32(char32_t *pc32, const char *s, size_t n, mbstate_t *ps);
size_t c32rtomb(char *s, char32_t c32, mbstate_t *ps);

#endif // _UCHAR_H
