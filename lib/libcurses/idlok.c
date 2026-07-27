// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// Turn the use of insert/delete-line sequences on and off for the given window.
//
#include "internal.h"

void idlok(WINDOW *win, int bf)
{
    if (bf)
        win->_flags |= _IDLINE;
    else
        win->_flags &= ~_IDLINE;
}
