// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// The mvprintw commands.  Because the argument count varies they cannot be macros, which is
// the whole reason this file exists.  printw.c has the account of the formatting core.
//
#include "internal.h"

int mvprintw(int y, int x, char *fmt, ...)
{
    va_list args;
    int ret;

    if (move(y, x) != OK)
        return ERR;
    va_start(args, fmt);
    ret = _sprintw(stdscr, fmt, args);
    va_end(args);
    return ret;
}

int mvwprintw(WINDOW *win, int y, int x, char *fmt, ...)
{
    va_list args;
    int ret;

    if (wmove(win, y, x) != OK)
        return ERR;
    va_start(args, fmt);
    ret = _sprintw(win, fmt, args);
    va_end(args);
    return ret;
}
