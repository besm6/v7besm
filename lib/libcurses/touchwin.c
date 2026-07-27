// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// Make it look to wrefresh() as though the whole window, or one line of it, has changed.
//
// _firstch[]/_lastch[] are kept in the PARENT's column numbering, which is what _ch_off is
// for: a subwindow shares its parent's change vectors (newwin.c's _set_subwin_) and must
// therefore bias every column it reports.  A top-level window's _ch_off is zero.
//
#include "internal.h"

int touchwin(WINDOW *win)
{
    int y, maxy;

    maxy = win->_maxy;
    for (y = 0; y < maxy; y++)
        touchline(win, y, 0, win->_maxx - 1);
    return OK;
}

// Touch a given line, from column sx to column ex inclusive.
int touchline(WINDOW *win, int y, int sx, int ex)
{
    sx += win->_ch_off;
    ex += win->_ch_off;
    if (win->_firstch[y] == _NOCHANGE) {
        win->_firstch[y] = sx;
        win->_lastch[y]  = ex;
    } else {
        if (win->_firstch[y] > sx)
            win->_firstch[y] = sx;
        if (win->_lastch[y] < ex)
            win->_lastch[y] = ex;
    }
    return OK;
}
