// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// Enter and leave standout mode.  The flag goes on the window; waddch() then sets _STANDOUT
// in every cell it writes, and refresh.c emits SO/SE around the runs that carry it.
//
// The return value is the capability string, or NULL if the terminal has neither SO nor UC.
// v7 returned FALSE here, which is (0) and therefore a valid null pointer constant, but says
// the wrong thing about the type.
//
#include "internal.h"

char *wstandout(WINDOW *win)
{
    if (!SO && !UC)
        return NULL;

    win->_flags |= _STANDOUT;
    return SO ? SO : UC;
}

char *wstandend(WINDOW *win)
{
    if (!SO && !UC)
        return NULL;

    win->_flags &= ~_STANDOUT;
    return SE ? SE : UC;
}
