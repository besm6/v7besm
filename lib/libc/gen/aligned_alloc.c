//
// aligned_alloc -- C11 §7.22.3.1.  Not a v7 routine: v7 had no such thing.
//
// There is almost nothing to do.  Every block malloc() returns begins at byte #0 of a
// word, and a word is the coarsest alignment this machine has -- there is no unit above
// it to align to, and none below it that a word-aligned block does not already satisfy.
// So any supported alignment is met by malloc() alone.
//
// An alignment WIDER than a word is not supported, and the standard's answer to an
// unsupported alignment is a null pointer rather than a wider block: nothing in the
// allocator could produce one, since the arena is a chain of one-word headers and a
// block's address is wherever the chain reached.
//
#include <stdlib.h>

#define NBPW 6 // char-units per word: sizeof(int)

void *aligned_alloc(size_t alignment, size_t size)
{
    if (alignment == 0 || alignment > NBPW)
        return NULL;
    return malloc(size);
}
