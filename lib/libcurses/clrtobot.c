// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// Clear from the cursor to the bottom of the window.
//
// Two `char *' cursors became int indices; clrtoeol.c has the account of why a relational
// between two of them could not be trusted then.  Unlike clrtoeol(), this routine's
// minx/maxx pair is LIVE -- touchline() is called only for the lines that actually changed,
// and with the exact columns -- so the conditional scan stays and only the cursors change
// type.  `maxx - &win->_y[y][0]' was already a subtraction and simply becomes `maxx'.
//
#include "internal.h"

void wclrtobot(WINDOW *win)
{
    int y, i;
    char *row;
    int startx, minx, maxx;

    startx = win->_curx;
    for (y = win->_cury; y < win->_maxy; y++) {
        minx = _NOCHANGE;
        maxx = 0;
        row  = win->_y[y];
        for (i = startx; i < win->_maxx; i++)
            if (row[i] != ' ') {
                maxx = i;
                if (minx == _NOCHANGE)
                    minx = i;
                row[i] = ' ';
            }
        if (minx != _NOCHANGE)
            touchline(win, y, minx, maxx);
        startx = 0;
    }
}
