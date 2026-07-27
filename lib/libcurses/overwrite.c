// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// Write win1 onto win2 destructively: every overlapping cell is copied, blanks included.
//
// `endy' read win2->_maxy + win2->_begX in 4.3BSD, where _begY is meant; the same slip is in
// overlay.c and toucholap.c, all three fixed.  bcopy() is memmove() -- argument order
// reversed -- because two windows can be subwindows of one parent and share their rows.
//
#include "internal.h"
#include <string.h>

#define min(a, b) (a < b ? a : b)
#define max(a, b) (a > b ? a : b)

void overwrite(WINDOW *win1, WINDOW *win2)
{
    int x, y, endy, endx, starty, startx;

    starty = max(win1->_begy, win2->_begy);
    startx = max(win1->_begx, win2->_begx);
    endy   = min(win1->_maxy + win1->_begy, win2->_maxy + win2->_begy);
    endx   = min(win1->_maxx + win1->_begx, win2->_maxx + win2->_begx);
    if (starty >= endy || startx >= endx)
        return;
    x = endx - startx;
    for (y = starty; y < endy; y++) {
        memmove(&win2->_y[y - win2->_begy][startx - win2->_begx],
                &win1->_y[y - win1->_begy][startx - win1->_begx], x);
        touchline(win2, y, startx - win2->_begx, endx - win2->_begx);
    }
}
