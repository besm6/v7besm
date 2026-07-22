// <wchar.h> -- extended multibyte and wide character utilities (C11 §7.29),
// BESM-6 target.
//
// Adapted from the c-compiler's libc/besm6/include/wchar.h.
//
// The canonical home of wint_t, mbstate_t and WEOF; <wctype.h> and <uchar.h>
// include it for them.  wchar_t itself comes from <stddef.h>, which is the
// compiler's header, since the compiler is what fixes the type.
//
// Both are one word.  The execution character set is ASCII (this terminal is
// ASCII, not KOI7 -- kernel/dev/sc.c) and MB_CUR_MAX is 1, so every conversion
// below is the identity on a single byte and mbstate_t never has a partial
// sequence to remember.  It is a struct all the same, because §7.29.1 requires a
// complete object type and a program may declare one.
//
// TODO: everything here, in libc.  The surface exists so a portable source
// compiles; the wide string routines are a rename of the <string.h> family with
// the byte loop turned into a word loop.
#ifndef _WCHAR_H
#define _WCHAR_H

#include <stdarg.h>
#include <stddef.h> // wchar_t, size_t, NULL

typedef int wint_t;

typedef struct {
    int __count;
    unsigned __value;
} mbstate_t;

#define WEOF ((wint_t) - 1)

// ---- declared for future implementation (TODO) ----
size_t wcslen(const wchar_t *s);
wchar_t *wcscpy(wchar_t *dest, const wchar_t *src);
wchar_t *wcsncpy(wchar_t *dest, const wchar_t *src, size_t n);
wchar_t *wcscat(wchar_t *dest, const wchar_t *src);
wchar_t *wcsncat(wchar_t *dest, const wchar_t *src, size_t n);
int wcscmp(const wchar_t *s1, const wchar_t *s2);
int wcsncmp(const wchar_t *s1, const wchar_t *s2, size_t n);
wchar_t *wcschr(const wchar_t *s, wchar_t c);
wchar_t *wcsrchr(const wchar_t *s, wchar_t c);
wchar_t *wcsstr(const wchar_t *haystack, const wchar_t *needle);
wchar_t *wcstok(wchar_t *s, const wchar_t *delim, wchar_t **ptr);

wchar_t *wmemcpy(wchar_t *dest, const wchar_t *src, size_t n);
wchar_t *wmemmove(wchar_t *dest, const wchar_t *src, size_t n);
wchar_t *wmemset(wchar_t *s, wchar_t c, size_t n);
int wmemcmp(const wchar_t *s1, const wchar_t *s2, size_t n);
wchar_t *wmemchr(const wchar_t *s, wchar_t c, size_t n);

wint_t btowc(int c);
int wctob(wint_t c);
int mbsinit(const mbstate_t *ps);
size_t mbrlen(const char *s, size_t n, mbstate_t *ps);
size_t mbrtowc(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps);
size_t wcrtomb(char *s, wchar_t wc, mbstate_t *ps);
size_t mbsrtowcs(wchar_t *dst, const char **src, size_t len, mbstate_t *ps);
size_t wcsrtombs(char *dst, const wchar_t **src, size_t len, mbstate_t *ps);

long wcstol(const wchar_t *nptr, wchar_t **endptr, int base);
unsigned long wcstoul(const wchar_t *nptr, wchar_t **endptr, int base);
double wcstod(const wchar_t *nptr, wchar_t **endptr);

#endif // _WCHAR_H
