// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// Clean up before leaving curses: put the tty modes back the way savetty() found them, take
// the terminal out of visual mode, and leave standout if we are in it.
//
#include "internal.h"

void endwin(void)
{
    resetty();
    _puts(VE);
    _puts(TE);
    if (curscr) {
        if (curscr->_flags & _STANDOUT) {
            _puts(SE);
            curscr->_flags &= ~_STANDOUT;
        }
        _endwin = TRUE;
    }
}
