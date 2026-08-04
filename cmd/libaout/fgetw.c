
#include <stdio.h>

#include "besm6/b.out.h"
#include "fastio.h"

// One full word == 48 bits == 6 bytes, read as two big-endian 24-bit
// half-words, high half-word first, so the six bytes form a plain big-endian
// 48-bit number.
//
// On the BESM-6 an aligned stdio cursor already holds that word -- see fastio.h.
uword_t fgetw(FILE *f)
{
#if besm6
    if (WORD_IN_BUF(f)) {
        uword_t *w = (uword_t *)f->_ptr;

        f->_ptr = (char *)(w + 1); // through a word pointer: b$padd never runs
        f->_cnt -= sizeof(uword_t);
        return *w;
    }
#endif
    {
        uword_t hi = (uword_t)fgeth(f);
        uword_t lo = (uword_t)fgeth(f);

        return (hi << 24) | lo;
    }
}
