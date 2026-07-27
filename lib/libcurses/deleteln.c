// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// Delete a line from the window, leaving (_cury, _curx) unchanged.
//
// A top-level window ROTATES ITS ROW POINTERS -- which is why its subwindows have to be
// re-aimed afterwards (_id_subwins) -- while a subwindow, whose rows belong to its parent,
// has to move the characters.  Both paths turn on _orig, so makenew() in newwin.c must
// initialise it; in 4.3BSD it did not.
//
// bcopy() is memmove() here -- ARGUMENT ORDER REVERSED -- and there is no <strings.h> in
// this tree.  memmove rather than memcpy because a subwindow's rows are windows onto the
// parent's, so two of them can overlap.  The trailing blank fill was v7's fourth `char *'
// relational (`for (end = &temp[_maxx]; temp < end; ) *temp++ = ' ';') and is a memset;
// clrtoeol.c has the account of why that comparison could not stay.
//
#include "internal.h"
#include <string.h>

int wdeleteln(WINDOW *win)
{
    char *temp;
    int y;

    temp = win->_y[win->_cury];
    for (y = win->_cury; y < win->_maxy - 1; y++) {
        if (win->_orig == NULL)
            win->_y[y] = win->_y[y + 1];
        else
            memmove(win->_y[y], win->_y[y + 1], win->_maxx);
        touchline(win, y, 0, win->_maxx - 1);
    }
    if (win->_orig == NULL)
        win->_y[y] = temp;
    else
        temp = win->_y[y];
    memset(temp, ' ', win->_maxx);
    touchline(win, win->_cury, 0, win->_maxx - 1);
    if (win->_orig == NULL)
        _id_subwins(win);
    return OK;
}
