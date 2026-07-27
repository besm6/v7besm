// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// scanw and friends: read a line through the window, then scan it.
//
// REAL VARARGS, NOT v7's `int args' HACK.  4.3BSD declared these `scanw(char *fmt, int args)'
// and passed `&args' down, on the assumption that the last named parameter sits at the head
// of the argument list in memory.  That assumption is not available here at all: <stdarg.h>
// is the external compiler's and every argument occupies one whole word by its own rules
// (../README.md), so an `int *' is not a va_list and never becomes one.  All four entry
// points take `...'.
//
// And no punned FILE -- printw.c has that account; the counterpart here is vsscanf
// (../libc/stdio/vsscanf.c) in place of a stack FILE with _IOREAD|_IOSTRG and _doscan.
//
#include "internal.h"

// A line of input.  17 words of frame; wgetstr() has no bound of its own (see getstr.c), so
// this is the number that matters.
#define SCANW_BUF 100

// The varargs core the four entry points here and in mvscanw.c share.
int _sscans(WINDOW *win, char *fmt, va_list ap)
{
    char buf[SCANW_BUF];

    if (wgetstr(win, buf) == ERR)
        return ERR;
    return vsscanf(buf, fmt, ap);
}

int scanw(char *fmt, ...)
{
    va_list args;
    int ret;

    va_start(args, fmt);
    ret = _sscans(stdscr, fmt, args);
    va_end(args);
    return ret;
}

int wscanw(WINDOW *win, char *fmt, ...)
{
    va_list args;
    int ret;

    va_start(args, fmt);
    ret = _sscans(win, fmt, args);
    va_end(args);
    return ret;
}
