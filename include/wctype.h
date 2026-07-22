// <wctype.h> -- wide character classification and mapping (C11 §7.30),
// BESM-6 target.
//
// Adapted from the c-compiler's libc/besm6/include/wctype.h.
//
// The wide classes are the narrow ones: the execution character set is ASCII,
// so iswalpha is isalpha applied to a value that happens to be a word wide, and
// there is nothing above 0177 to classify.  Whether they end up as calls into
// the same _ctype_ table <ctype.h> uses is an implementation question for
// whoever writes them; the table's 129 entries already answer every wint_t that
// is not WEOF.
//
// TODO: everything here, in libc.
#ifndef _WCTYPE_H
#define _WCTYPE_H

#include <wchar.h> // wint_t, WEOF

typedef int wctype_t;
typedef int wctrans_t;

// ---- declared for future implementation (TODO) ----
int iswalnum(wint_t wc);
int iswalpha(wint_t wc);
int iswblank(wint_t wc);
int iswcntrl(wint_t wc);
int iswdigit(wint_t wc);
int iswgraph(wint_t wc);
int iswlower(wint_t wc);
int iswprint(wint_t wc);
int iswpunct(wint_t wc);
int iswspace(wint_t wc);
int iswupper(wint_t wc);
int iswxdigit(wint_t wc);

wint_t towlower(wint_t wc);
wint_t towupper(wint_t wc);

wctype_t wctype(const char *property);
int iswctype(wint_t wc, wctype_t desc);
wctrans_t wctrans(const char *property);
wint_t towctrans(wint_t wc, wctrans_t desc);

#endif // _WCTYPE_H
