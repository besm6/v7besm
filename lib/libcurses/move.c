// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// Move the cursor to the given point within the window.
//
#include "internal.h"

int wmove(WINDOW *win, int y, int x)
{
    if (x < 0 || y < 0)
        return ERR;
    if (x >= win->_maxx || y >= win->_maxy)
        return ERR;
    win->_curx = x;
    win->_cury = y;
    return OK;
}
