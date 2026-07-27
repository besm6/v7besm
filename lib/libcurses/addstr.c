// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// Add a string starting at (_cury, _curx).
//
#include "internal.h"

int waddstr(WINDOW *win, char *str)
{
    while (*str)
        if (waddch(win, *str++) == ERR)
            return ERR;
    return OK;
}
