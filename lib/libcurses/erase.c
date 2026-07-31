// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// Erase everything on the window and put the cursor at its top left.
//
// Same rewrite and the same reason as clrtobot.c: the cursors are int indices, and the
// minx/maxx pair is live so the conditional scan stays.
//
#include "internal.h"

void werase(WINDOW *win)
{
    int y, i;
    char *row;
    int minx, maxx;

    for (y = 0; y < win->_maxy; y++) {
        minx = _NOCHANGE;
        maxx = 0;
        row  = win->_y[y];
        for (i = 0; i < win->_maxx; i++)
            if (row[i] != ' ') {
                maxx = i;
                if (minx == _NOCHANGE)
                    minx = i;
                row[i] = ' ';
            }
        if (minx != _NOCHANGE)
            touchline(win, y, minx, maxx);
    }
    win->_curx = win->_cury = 0;
}
