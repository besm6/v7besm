// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// Read one character through the window, echoing it into the window if echo() is on.
//
// THE CHARACTER IS AN int, NOT A char, and on this machine that is not pedantry.  4.3BSD
// wrote `char inp = getchar();' and never tested for EOF; `char' is UNSIGNED here
// (../../doc/Besm6_Data_Representation.md), so EOF (-1) became 0377 -- a perfectly ordinary
// character -- and wgetch() could never return ERR.  getstr.c's loop reads until ERR or a
// newline, so on a stream that simply ended it never stopped.  On a machine with signed
// `char' the same code limps: EOF survives as -1 and compares unequal to ERR, which is 0,
// so the loop still runs away.  It is upstream's bug either way; unsigned `char' only takes
// away the last accident that hid it.
//
#include "internal.h"

int wgetch(WINDOW *win)
{
    int weset = FALSE;
    int inp;

    if (!win->_scroll && (win->_flags & _FULLWIN) && win->_curx == win->_maxx - 1 &&
        win->_cury == win->_maxy - 1)
        return ERR;

    // Reading one character at a time means the line discipline must not be holding it: if
    // the tty is still cooked, drop into cbreak for the duration of this call.
    if (_echoit && !_rawmode) {
        cbreak();
        weset = TRUE;
    }
    inp = getchar();
    if (inp == EOF) {
        if (weset)
            nocbreak();
        return ERR;
    }
    if (_echoit) {
        mvwaddch(curscr, win->_cury + win->_begy, win->_curx + win->_begx, inp);
        waddch(win, inp);
    }
    if (weset)
        nocbreak();
    return inp;
}
