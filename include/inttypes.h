// <inttypes.h> -- format conversion of integer types (C11 §7.8), BESM-6 target.
//
// Because every integer type is one word, the length modifiers all collapse: the
// print/scan format macros are plain "d"/"u"/"o"/"x" with no length prefix (the
// printf engine ignores 'l'/'h' anyway -- see doprnt.c).
//
// Which macros exist follows exactly which types <stdint.h> provides, as §7.8.1
// requires -- "for each type ... that the implementation provides", and no
// others.  So:
//
//   - the exact-width set is int8_t/uint8_t ALONE.  There are no 16-, 32- or
//     64-bit types on this machine, so there is no PRId16, PRId32 or PRId64; an
//     earlier version of this header defined the first two, naming types that do
//     not exist.
//   - the minimum-width and fastest-width sets run 8, 16, 32.  int_least16_t,
//     int_fast32_t and their kin ARE provided -- every one of them is the native
//     word, which holds at least 32 bits -- so their macros are required, and
//     were the real omission here.  N = 64 is absent throughout: the widest
//     signed value is 41 bits.
//   - intmax_t is only 41-bit signed and uintmax_t 48-bit unsigned, which is why
//     imaxdiv_t below is a pair of words like every other div_t in this tree.
//
// TODO: imaxabs/imaxdiv/strtoimax/strtoumax in libc.
#ifndef _INTTYPES_H
#define _INTTYPES_H

#include <stdint.h>

typedef struct {
    intmax_t quot;
    intmax_t rem;
} imaxdiv_t;

intmax_t imaxabs(intmax_t j);
imaxdiv_t imaxdiv(intmax_t numer, intmax_t denom);
intmax_t strtoimax(const char *nptr, char **endptr, int base);
uintmax_t strtoumax(const char *nptr, char **endptr, int base);

// ---- print format macros ----
#define PRId8       "d"
#define PRIdLEAST8  "d"
#define PRIdLEAST16 "d"
#define PRIdLEAST32 "d"
#define PRIdFAST8   "d"
#define PRIdFAST16  "d"
#define PRIdFAST32  "d"
#define PRIdMAX     "d"
#define PRIdPTR     "d"

#define PRIi8       "i"
#define PRIiLEAST8  "i"
#define PRIiLEAST16 "i"
#define PRIiLEAST32 "i"
#define PRIiFAST8   "i"
#define PRIiFAST16  "i"
#define PRIiFAST32  "i"
#define PRIiMAX     "i"
#define PRIiPTR     "i"

#define PRIu8       "u"
#define PRIuLEAST8  "u"
#define PRIuLEAST16 "u"
#define PRIuLEAST32 "u"
#define PRIuFAST8   "u"
#define PRIuFAST16  "u"
#define PRIuFAST32  "u"
#define PRIuMAX     "u"
#define PRIuPTR     "u"

#define PRIo8       "o"
#define PRIoLEAST8  "o"
#define PRIoLEAST16 "o"
#define PRIoLEAST32 "o"
#define PRIoFAST8   "o"
#define PRIoFAST16  "o"
#define PRIoFAST32  "o"
#define PRIoMAX     "o"
#define PRIoPTR     "o"

#define PRIx8       "x"
#define PRIxLEAST8  "x"
#define PRIxLEAST16 "x"
#define PRIxLEAST32 "x"
#define PRIxFAST8   "x"
#define PRIxFAST16  "x"
#define PRIxFAST32  "x"
#define PRIxMAX     "x"
#define PRIxPTR     "x"

#define PRIX8       "X"
#define PRIXLEAST8  "X"
#define PRIXLEAST16 "X"
#define PRIXLEAST32 "X"
#define PRIXFAST8   "X"
#define PRIXFAST16  "X"
#define PRIXFAST32  "X"
#define PRIXMAX     "X"
#define PRIXPTR     "X"

// ---- scan format macros ----
#define SCNd8       "d"
#define SCNdLEAST8  "d"
#define SCNdLEAST16 "d"
#define SCNdLEAST32 "d"
#define SCNdFAST8   "d"
#define SCNdFAST16  "d"
#define SCNdFAST32  "d"
#define SCNdMAX     "d"
#define SCNdPTR     "d"

#define SCNi8       "i"
#define SCNiLEAST8  "i"
#define SCNiLEAST16 "i"
#define SCNiLEAST32 "i"
#define SCNiFAST8   "i"
#define SCNiFAST16  "i"
#define SCNiFAST32  "i"
#define SCNiMAX     "i"
#define SCNiPTR     "i"

#define SCNu8       "u"
#define SCNuLEAST8  "u"
#define SCNuLEAST16 "u"
#define SCNuLEAST32 "u"
#define SCNuFAST8   "u"
#define SCNuFAST16  "u"
#define SCNuFAST32  "u"
#define SCNuMAX     "u"
#define SCNuPTR     "u"

#define SCNo8       "o"
#define SCNoLEAST8  "o"
#define SCNoLEAST16 "o"
#define SCNoLEAST32 "o"
#define SCNoFAST8   "o"
#define SCNoFAST16  "o"
#define SCNoFAST32  "o"
#define SCNoMAX     "o"
#define SCNoPTR     "o"

#define SCNx8       "x"
#define SCNxLEAST8  "x"
#define SCNxLEAST16 "x"
#define SCNxLEAST32 "x"
#define SCNxFAST8   "x"
#define SCNxFAST16  "x"
#define SCNxFAST32  "x"
#define SCNxMAX     "x"
#define SCNxPTR     "x"

#endif // _INTTYPES_H
