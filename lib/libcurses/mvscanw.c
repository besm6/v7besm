// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// The mvscanw commands.  As with mvprintw, the varying argument count is why these are
// functions and not macros; scanw.c has the account of the scanning core.
//
#include "internal.h"

int mvscanw(int y, int x, char *fmt, ...)
{
    va_list args;
    int ret;

    if (move(y, x) != OK)
        return ERR;
    va_start(args, fmt);
    ret = _sscans(stdscr, fmt, args);
    va_end(args);
    return ret;
}

int mvwscanw(WINDOW *win, int y, int x, char *fmt, ...)
{
    va_list args;
    int ret;

    if (wmove(win, y, x) != OK)
        return ERR;
    va_start(args, fmt);
    ret = _sscans(win, fmt, args);
    va_end(args);
    return ret;
}
