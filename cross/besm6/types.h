#ifndef BESM6_TYPES_H
#define BESM6_TYPES_H

#if besm6

//
// Native build.
//
typedef int word_t;
typedef unsigned uword_t;

#else

//
// Cross build.
//
#include <stdint.h>

typedef int64_t word_t;
typedef uint64_t uword_t;

#endif

#endif // BESM6_TYPES_H
