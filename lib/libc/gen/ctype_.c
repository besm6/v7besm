/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * The character-class table the <ctype.h> macros index.
 *
 * Unchanged from v7, because the terminal is ASCII and not KOI7 (kernel/dev/sc.c):
 * the classes fall where the v7 table says they do.  129 entries, not 128 -- every
 * macro indexes `(_ctype_ + 1)[c]', so EOF (-1) lands on the leading zero and comes
 * out as no class at all.
 *
 * `char' here means one BYTE, six to a word, so the whole table is 22 words; the
 * macros' subscript is fat-pointer arithmetic, which walks across the word boundaries
 * on its own.  A subscript of 128..255 runs off the end, as it does on every v7
 * machine: only isascii() may be applied to those.
 */
#include <ctype.h>

char _ctype_[] = {
    // clang-format off
    0,
    _C,     _C,     _C,     _C,     _C,     _C,     _C,     _C,
    _C,     _S,     _S,     _S,     _S,     _S,     _C,     _C,
    _C,     _C,     _C,     _C,     _C,     _C,     _C,     _C,
    _C,     _C,     _C,     _C,     _C,     _C,     _C,     _C,
    _S,     _P,     _P,     _P,     _P,     _P,     _P,     _P,
    _P,     _P,     _P,     _P,     _P,     _P,     _P,     _P,
    _N,     _N,     _N,     _N,     _N,     _N,     _N,     _N,
    _N,     _N,     _P,     _P,     _P,     _P,     _P,     _P,
    _P,     _U|_X,  _U|_X,  _U|_X,  _U|_X,  _U|_X,  _U|_X,  _U,
    _U,     _U,     _U,     _U,     _U,     _U,     _U,     _U,
    _U,     _U,     _U,     _U,     _U,     _U,     _U,     _U,
    _U,     _U,     _U,     _P,     _P,     _P,     _P,     _P,
    _P,     _L|_X,  _L|_X,  _L|_X,  _L|_X,  _L|_X,  _L|_X,  _L,
    _L,     _L,     _L,     _L,     _L,     _L,     _L,     _L,
    _L,     _L,     _L,     _L,     _L,     _L,     _L,     _L,
    _L,     _L,     _L,     _P,     _P,     _P,     _P,     _C,
    // clang-format on
};
