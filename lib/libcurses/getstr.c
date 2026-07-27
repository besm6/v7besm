// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// Read a string through the window, up to a newline.
//
// The character is held in an `int' before it is stored.  v7 assigned wgetch()'s result
// straight into *str and then compared *str against ERR, which cannot distinguish a
// truncated character from end of input -- see the note in getch.c about what `char' being
// unsigned does to EOF here.
//
// There is still no bound on `str'.  That is v7's interface and gets(3)'s bargain, and
// there is no way to pass a size through it; the callers in this tree (_sscans in scanw.c)
// hand it a buffer they size themselves.
//
#include "internal.h"

int wgetstr(WINDOW *win, char *str)
{
    int c;

    while ((c = wgetch(win)) != ERR && c != '\n')
        *str++ = c;
    *str = '\0';
    return c == ERR ? ERR : OK;
}
