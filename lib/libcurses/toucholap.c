// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// Touch, on win2, the part that overlaps win1.
//
// `endy' read win2->_maxy + win2->_begX in 4.3BSD, where _begY is meant -- the same
// transcription slip appears in overlay.c and overwrite.c, all three fixed here.  It is
// invisible whenever _begx == _begy, which is true of every window initscr() makes and of
// any window placed on a diagonal, which is why it survived.
//
#include "internal.h"

#define min(a, b) (a < b ? a : b)
#define max(a, b) (a > b ? a : b)

void touchoverlap(WINDOW *win1, WINDOW *win2)
{
    int y, endy, endx, starty, startx;

    starty = max(win1->_begy, win2->_begy);
    startx = max(win1->_begx, win2->_begx);
    endy   = min(win1->_maxy + win1->_begy, win2->_maxy + win2->_begy);
    endx   = min(win1->_maxx + win1->_begx, win2->_maxx + win2->_begx);
    if (starty >= endy || startx >= endx)
        return;
    starty -= win2->_begy;
    startx -= win2->_begx;
    endy -= win2->_begy;
    endx -= win2->_begx;
    endx--;
    for (y = starty; y < endy; y++)
        touchline(win2, y, startx, endx);
}
