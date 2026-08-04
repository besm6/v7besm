
#include <stdio.h>

#include "besm6/b.out.h"
#include "fastio.h"

// One full word == 48 bits == 6 bytes, stored as two big-endian 24-bit
// half-words, high half-word first, so the six bytes form a plain big-endian
// 48-bit number.
//
// On the BESM-6 an aligned stdio cursor takes that word whole -- see fastio.h.
void fputw(uword_t w, FILE *f)
{
#if besm6
    if (WORD_IN_BUF(f)) {
        uword_t *p = (uword_t *)f->_ptr;

        *p      = w;
        f->_ptr = (char *)(p + 1); // through a word pointer: b$padd never runs
        f->_cnt -= sizeof(uword_t);
        return;
    }
#endif
    fputh((long)((w >> 24) & 0xFFFFFF), f);
    fputh((long)(w & 0xFFFFFF), f);
}
