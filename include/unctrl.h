// unctrl.h -- render a character printably.
//
// 1/26/81 (Berkeley) @(#)unctrl.h	1.1
//
// _unctrl[] is 128 entries and lives in lib/libcurses/unctrl.c.  THE SUBSCRIPT IS MASKED,
// which v7's spelling -- `_unctrl[(unsigned)ch]' -- was not.  Two reasons it has to be:
// a window cell carries _STANDOUT in bit 0200 (<curses.h>), so a character taken straight
// out of one is routinely above 0177; and a cast to `unsigned' does not widen a negative
// value here the way it does on a two's-complement byte machine -- an `int' is 41 bits
// signed and an `unsigned' 48 (../doc/Besm6_Data_Representation.md), so (unsigned)(-1) is
// 2**41-1, and indexing a 128-entry array with it reads somewhere else entirely.
//
// 4.3BSD's <curses.h> defined this macro a second time, itself; ours includes this header
// instead, so there is one definition and a program may include both.

#ifndef _UNCTRL_H
#define _UNCTRL_H

extern char *_unctrl[];

#define unctrl(ch) (_unctrl[(ch) & 0177])

#endif // _UNCTRL_H
