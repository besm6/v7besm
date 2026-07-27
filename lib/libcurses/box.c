// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// Draw a box around the window with `vert' as the vertical delimiting character and `hor'
// as the horizontal one.
//
// The four corners of a scrolling window that the caller has not made scrollable are left
// blank: writing the bottom-right cell of such a window is what would make waddch() scroll,
// and a box is not worth a scroll.
//
#include "internal.h"

void box(WINDOW *win, char vert, char hor)
{
    int i;
    int endy, endx;
    char *fp, *lp;

    endx = win->_maxx;
    endy = win->_maxy - 1;
    fp   = win->_y[0];
    lp   = win->_y[endy];
    for (i = 0; i < endx; i++)
        fp[i] = lp[i] = hor;
    endx--;
    for (i = 0; i <= endy; i++)
        win->_y[i][0] = (win->_y[i][endx] = vert);
    if (!win->_scroll && (win->_flags & _SCROLLWIN))
        fp[0] = fp[endx] = lp[0] = lp[endx] = ' ';
    touchwin(win);
}
