// <locale.h> -- localization (C11 §7.11), BESM-6 target.
//
// Adapted from the c-compiler's libc/besm6/include/locale.h, which ships this
// header but does not install it: the hosted half of the tree is this repo's.
//
// There is one locale and it is "C".  setlocale() therefore answers "C" for any
// category when asked for "C" or "", and a null pointer for anything else; that
// is a conforming implementation of §7.11.1.1, not a stub, and it is all this
// machine will ever need.  localeconv() returns the §7.11.2.1 "C" values.
//
// TODO: both routines, in libc.  The surface is here so a portable source
// compiles.
#ifndef _LOCALE_H
#define _LOCALE_H

#include <stddef.h> // NULL

#define LC_ALL      0
#define LC_COLLATE  1
#define LC_CTYPE    2
#define LC_MONETARY 3
#define LC_NUMERIC  4
#define LC_TIME     5

// §7.11.1.1 gives struct lconv 24 members and lets the order be anything; this
// is the standard's own order.  The char-typed members hold a small count or a
// boolean, not text, and CHAR_MAX in one of them means "not available in this
// locale" -- which, in the "C" locale, is every one of the monetary ones.
struct lconv {
    char *decimal_point;
    char *thousands_sep;
    char *grouping;
    char *mon_decimal_point;
    char *mon_thousands_sep;
    char *mon_grouping;
    char *positive_sign;
    char *negative_sign;
    char *currency_symbol;
    char frac_digits;
    char p_cs_precedes;
    char n_cs_precedes;
    char p_sep_by_space;
    char n_sep_by_space;
    char p_sign_posn;
    char n_sign_posn;
    char *int_curr_symbol;
    char int_frac_digits;
    char int_p_cs_precedes;
    char int_n_cs_precedes;
    char int_p_sep_by_space;
    char int_n_sep_by_space;
    char int_p_sign_posn;
    char int_n_sign_posn;
};

// ---- declared for future implementation (TODO) ----
char *setlocale(int category, const char *locale);
struct lconv *localeconv(void);

#endif // _LOCALE_H
