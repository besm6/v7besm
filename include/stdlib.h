// <stdlib.h> -- general utilities (C11 §7.22), BESM-6 target.
//
// Adapted from the c-compiler's tree, and the status notes below are now this
// repo's: what libc.a has, not what that project's libc.bin/libc0.a had.
//
// `long' is `int', one word, so several of these collapse onto each other: atol
// IS atoi and is written as a call to it, labs is abs, and ldiv_t is div_t with
// a different name.  Each pair is still declared separately, because a portable
// source names both.
#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

// RAND_MAX is bounded by the signed integer ceiling (2^40-1).
#define RAND_MAX 1099511627775L

typedef struct {
    int quot;
    int rem;
} div_t;

typedef struct {
    long quot;
    long rem;
} ldiv_t;

// The largest value MB_CUR_MAX may take (§7.22p3).  One, and it cannot be more:
// the execution character set is ASCII, one byte per character, and the
// multibyte conversions of <wchar.h> are the identity on it.
#define MB_CUR_MAX 1

// ---- implemented in libc.a ----
_Noreturn void exit(int status);
_Noreturn void abort(void);
int atoi(const char *nptr);
long atol(const char *nptr);
int abs(int j);
int rand(void);
void srand(unsigned seed);
void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));
char *getenv(const char *name);

// The allocator: v7's, grown through sbrk() a page at a time.  free(NULL) is a
// no-op and realloc(NULL, n) is malloc(n), which C11 requires and v7 did not do.
void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);

// Every block this allocator returns already starts on a word boundary, and a
// word is the coarsest alignment the machine has, so aligned_alloc can only ever
// be malloc with its argument checked.
void *aligned_alloc(size_t alignment, size_t size);

// ascii to floating, over the native format's range (~10^±18).  It is here and
// not in <math.h> because C11 puts it here; ecvt/fcvt/gcvt, its inverses, are v7
// extensions no header declares and a caller declares them itself.
double atof(const char *nptr);

// ---- declared for future implementation (TODO) ----
_Noreturn void _Exit(int status);
int atexit(void (*func)(void));
_Noreturn void quick_exit(int status);
int at_quick_exit(void (*func)(void));

long strtol(const char *nptr, char **endptr, int base);
unsigned long strtoul(const char *nptr, char **endptr, int base);
double strtod(const char *nptr, char **endptr);

long labs(long j);
div_t div(int numer, int denom);
ldiv_t ldiv(long numer, long denom);

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *));

int system(const char *command);

// The multibyte group (§7.22.7-8).  The execution character set is ASCII and
// MB_CUR_MAX is 1, so every one of these is the identity on a single byte; they
// exist so a portable source that walks a string through them still compiles.
// wchar_t comes from <stddef.h> above.
int mblen(const char *s, size_t n);
int mbtowc(wchar_t *pwc, const char *s, size_t n);
int wctomb(char *s, wchar_t wc);
size_t mbstowcs(wchar_t *dest, const char *src, size_t n);
size_t wcstombs(char *dest, const wchar_t *src, size_t n);

#endif // _STDLIB_H
