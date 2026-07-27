// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// Delete the character under the cursor, shifting the rest of the line left and blanking
// the last column.  (_cury, _curx) is left where it was.
//
// v7's `while (temp1 < end)' compared two `char *'; clrtoeol.c has the account.  The index
// form below reproduces it exactly at the boundary too: when the cursor is already on the
// last column the loop body never runs and only that column is blanked, which is what v7's
// `temp1 == end' case did.
//
#include "internal.h"

int wdelch(WINDOW *win)
{
    char *row;
    int i;

    row = win->_y[win->_cury];
    for (i = win->_curx; i < win->_maxx - 1; i++)
        row[i] = row[i + 1];
    row[i] = ' ';
    touchline(win, win->_cury, win->_curx, win->_maxx - 1);
    return OK;
}
