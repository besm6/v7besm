// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// Scroll the window up a line.  On curscr that also means telling the terminal, which is
// done the only way that always works: print a newline on the bottom line.
//
#include "internal.h"

int scroll(WINDOW *win)
{
    int oy, ox;

    if (!win->_scroll)
        return ERR;

    getyx(win, oy, ox);
    wmove(win, 0, 0);
    wdeleteln(win);
    wmove(win, oy, ox);

    if (win == curscr) {
        _putchar('\n');
        if (!NONL)
            win->_curx = 0;
    }
    return OK;
}
