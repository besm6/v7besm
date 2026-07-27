// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// Add a character at the current position.
//
// A CELL IS SEVEN BITS OF CHARACTER PLUS _STANDOUT (0200), which is why nothing may hand a
// raw cell to <ctype.h> (see overlay.c) and why winch() masks.  `char' is unsigned on this
// machine, so the flag survives every widening without a sign-extension accident -- on a
// signed-char machine `_putchar(cell)' would pass a negative int.
//
#include "internal.h"

// Set the first and last change columns for this line of the window.
static void set_ch(WINDOW *win, int y, int x, int ch)
{
    if (win->_y[y][x] != ch) {
        x += win->_ch_off;
        if (win->_firstch[y] == _NOCHANGE)
            win->_firstch[y] = win->_lastch[y] = x;
        else if (x < win->_firstch[y])
            win->_firstch[y] = x;
        else if (x > win->_lastch[y])
            win->_lastch[y] = x;
    }
}

int waddch(WINDOW *win, char c)
{
    int x, y;
    int newx;

    x = win->_curx;
    y = win->_cury;
    switch (c) {
    case '\t':
        for (newx = x + (8 - (x & 07)); x < newx; x++)
            if (waddch(win, ' ') == ERR)
                return ERR;
        return OK;

    default:
        if (win->_flags & _STANDOUT)
            c |= _STANDOUT;
        set_ch(win, y, x, c);
        win->_y[y][x++] = c;
        if (x >= win->_maxx) {
            x = 0;
        newline:
            if (++y >= win->_maxy) {
                if (!win->_scroll)
                    return ERR;
                scroll(win);
                --y;
            }
        }
        break;
    case '\n':
        wclrtoeol(win);
        if (!NONL)
            x = 0;
        goto newline;
    case '\r':
        x = 0;
        break;
    case '\b':
        if (--x < 0)
            x = 0;
        break;
    }
    win->_curx = x;
    win->_cury = y;
    return OK;
}
