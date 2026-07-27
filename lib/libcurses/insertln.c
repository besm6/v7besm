// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// Insert a line into the window, leaving (_cury, _curx) unchanged.  The mirror of
// wdeleteln(); its header comment covers the row-rotation-versus-memmove split, the bcopy
// replacement and the blank fill.
//
#include "internal.h"
#include <string.h>

void winsertln(WINDOW *win)
{
    char *temp = NULL;
    int y;

    if (win->_orig == NULL)
        temp = win->_y[win->_maxy - 1];
    for (y = win->_maxy - 1; y > win->_cury; --y) {
        if (win->_orig == NULL)
            win->_y[y] = win->_y[y - 1];
        else
            memmove(win->_y[y], win->_y[y - 1], win->_maxx);
        touchline(win, y, 0, win->_maxx - 1);
    }
    if (win->_orig == NULL)
        win->_y[y] = temp;
    else
        temp = win->_y[y];
    memset(temp, ' ', win->_maxx);
    touchline(win, y, 0, win->_maxx - 1);
    if (win->_orig == NULL)
        _id_subwins(win);
}
