// <inttypes.h> — format conversion of integer types (C11 §7.8), BESM-6 target.
//
// Because every integer type is one word, the length modifiers all collapse:
// the print/scan format macros use plain "d"/"u"/"o"/"x" with no length prefix
// (the printf engine ignores 'l'/'h' anyway — see madlen/doprnt.c).
//
// TODO: imaxabs/imaxdiv/strtoimax/strtoumax in libc.bin.
#ifndef _INTTYPES_H
#define _INTTYPES_H

#include <stdint.h>

typedef struct {
    intmax_t quot;
    intmax_t rem;
} imaxdiv_t;

intmax_t  imaxabs(intmax_t j);
imaxdiv_t imaxdiv(intmax_t numer, intmax_t denom);
intmax_t  strtoimax(const char *nptr, char **endptr, int base);
uintmax_t strtoumax(const char *nptr, char **endptr, int base);

// print format macros
#define PRId8 "d"
#define PRId16 "d"
#define PRId32 "d"
#define PRIdMAX "d"
#define PRIdPTR "d"
#define PRIi8 "i"
#define PRIi16 "i"
#define PRIi32 "i"
#define PRIiMAX "i"
#define PRIiPTR "i"
#define PRIu8 "u"
#define PRIu16 "u"
#define PRIu32 "u"
#define PRIuMAX "u"
#define PRIuPTR "u"
#define PRIo8 "o"
#define PRIo16 "o"
#define PRIo32 "o"
#define PRIoMAX "o"
#define PRIoPTR "o"
#define PRIx8 "x"
#define PRIx16 "x"
#define PRIx32 "x"
#define PRIxMAX "x"
#define PRIxPTR "x"
#define PRIX8 "X"
#define PRIX16 "X"
#define PRIX32 "X"
#define PRIXMAX "X"
#define PRIXPTR "X"

// scan format macros
#define SCNd8 "d"
#define SCNd16 "d"
#define SCNd32 "d"
#define SCNdMAX "d"
#define SCNdPTR "d"
#define SCNu8 "u"
#define SCNu16 "u"
#define SCNu32 "u"
#define SCNuMAX "u"
#define SCNuPTR "u"
#define SCNx8 "x"
#define SCNx16 "x"
#define SCNx32 "x"
#define SCNxMAX "x"
#define SCNxPTR "x"

#endif // _INTTYPES_H
