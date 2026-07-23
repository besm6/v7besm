// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// calloc -- allocate and clear an array; cfree -- v7's spelling of free() for it.
//
// v7 clears the block with a loop of its own over `int's; here memset() is already in
// the archive and moves the same words, so the loop goes.  It clears NUM BYTES, not the
// whole block: the tail bytes of the last word are not part of the object, and a
// request that is not a multiple of six leaves them alone.
//
// The overflow test is new, and C11 §7.22.3.2 needs it: v7 multiplies and hands the
// product to malloc, so nmemb*size that wraps would quietly allocate a small block for
// a large array.  SIZE_MAX comes from the compiler's <stdint.h>; do NOT write it as
// (size_t)-1, which on this machine is 2^41-1 rather than 2^48-1 -- a signed-to-
// unsigned conversion is a bare copy of a 41-bit pattern, not an adjustment by 2^48
// (doc/Besm6_Data_Representation.md).
//
// cfree() is a v7 name and no header declares it -- it is not in C11 -- so a caller
// declares it itself, as it would index() or swab().
//
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void cfree(void *p, size_t num, size_t size);

void *calloc(size_t num, size_t size)
{
    void *mp;

    if (size != 0 && num > SIZE_MAX / size)
        return NULL;

    num *= size;
    mp = malloc(num);
    if (mp == NULL)
        return NULL;
    return memset(mp, 0, num);
}

void cfree(void *p, size_t num, size_t size)
{
    free(p);
}
