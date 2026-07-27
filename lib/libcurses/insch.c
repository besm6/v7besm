// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// Insert a character at the cursor, shifting the rest of the line right and dropping what
// falls off the end.  (_cury, _curx) is left where it was.
//
// v7's `while (temp1 > end)' compared two `char *'; clrtoeol.c has the account.
//
#include "internal.h"

int winsch(WINDOW *win, char c)
{
    char *row;
    int i;

    row = win->_y[win->_cury];
    for (i = win->_maxx - 1; i > win->_curx; i--)
        row[i] = row[i - 1];
    row[win->_curx] = c;
    touchline(win, win->_cury, win->_curx, win->_maxx - 1);
    return OK;
}
