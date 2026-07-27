// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// Write win1 onto win2 NON-destructively: a cell of win1 that holds a blank leaves win2's
// cell alone.  That is the whole difference from overwrite(), and it is what the name, the
// comment 4.3BSD put on this function, and curses.3 all promise.
//
// THE bcopy() 4.3BSD HAD HERE IS GONE.  This file carried overwrite.c's copy loop verbatim
// -- an unconditional row-at-a-time bcopy -- and then the selective loop below.  The first
// one copies the blanks too, so it makes overlay() destructive and leaves the second loop
// with nothing left to do: the two functions became the same function, and the one that
// documents itself as non-destructive was not.  Removed rather than kept, because keeping it
// means <curses.h> has two spellings of overwrite() and no overlay() at all.
//
// isspace() IS GONE TOO, and that one is this machine's doing.  A window cell carries
// _STANDOUT in bit 0200 (addch.c sets it), `char' is unsigned here, and _ctype_[] is 129
// entries indexed as (_ctype_ + 1)[c] -- so isspace() on a standout cell reads past the end
// of the table (see ../libc/gen/ctype_.c, which says so itself).  Every other blank test in
// this library is a comparison against ' ', and so is this one now, over the character with
// the standout bit masked off.
//
// The `char *' scan became an int index, for clrtoeol.c's reason.  And `endy' read
// win2->_maxy + win2->_begX where _begY is meant -- the slip shared with overwrite.c and
// toucholap.c.
//
#include "internal.h"

#define min(a, b) (a < b ? a : b)
#define max(a, b) (a > b ? a : b)

void overlay(WINDOW *win1, WINDOW *win2)
{
    char *row;
    int i, x, y, y1, y2, endy, endx, starty, startx;

    starty = max(win1->_begy, win2->_begy);
    startx = max(win1->_begx, win2->_begx);
    endy   = min(win1->_maxy + win1->_begy, win2->_maxy + win2->_begy);
    endx   = min(win1->_maxx + win1->_begx, win2->_maxx + win2->_begx);
    if (starty >= endy || startx >= endx)
        return;

    y1 = starty - win1->_begy;
    y2 = starty - win2->_begy;
    for (y = starty; y < endy; y++, y1++, y2++) {
        row = win1->_y[y1];
        x   = startx - win2->_begx;
        for (i = startx - win1->_begx; i < endx - win1->_begx; i++, x++)
            if ((row[i] & 0177) != ' ')
                mvwaddch(win2, y2, x, row[i]);
    }
}
