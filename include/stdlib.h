// <stdlib.h> — general utilities (C11 §7.22), BESM-6 target.
//
// Status: exit() and atoi() are implemented in the runtime library (Madlen
// libc.bin and Unix libc0.a).  The dynamic allocator (malloc/calloc/realloc/
// free) is implemented in the Unix libc0.a only — it depends on the b6ld/b6sim
// memory map and is absent from the Madlen libc.bin.  The rest are declared for
// future implementation (TODO).
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

// ---- implemented in libc.bin ----
_Noreturn void exit(int status);
int   atoi(const char *nptr);

// ---- implemented in the Unix libc0.a only (absent from Madlen libc.bin) ----
void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void  free(void *ptr);

// ---- declared for future implementation (TODO) ----
_Noreturn void abort(void);
int   atexit(void (*func)(void));

long  atol(const char *nptr);
double atof(const char *nptr);
long  strtol(const char *nptr, char **endptr, int base);
unsigned long strtoul(const char *nptr, char **endptr, int base);
double strtod(const char *nptr, char **endptr);

int   abs(int j);
long  labs(long j);
div_t  div(int numer, int denom);
ldiv_t ldiv(long numer, long denom);

int   rand(void);
void  srand(unsigned seed);

void  qsort(void *base, size_t nmemb, size_t size,
            int (*compar)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *));

char *getenv(const char *name);
int   system(const char *command);

#endif // _STDLIB_H
