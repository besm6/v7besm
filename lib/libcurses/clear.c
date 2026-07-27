// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// Clear the window: blank it now, and arrange for the next wrefresh() to clear the screen
// rather than compute a difference against it.
//
#include "internal.h"

int wclear(WINDOW *win)
{
    werase(win);
    win->_clear = TRUE;
    return OK;
}
