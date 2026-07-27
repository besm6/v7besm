// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// printw and friends: format into a buffer, then add the buffer to the window.
//
// NO FILE IS PUNNED HERE.  4.3BSD declared a `FILE junk' on the stack, set _flag to
// _IOWRT + _IOSTRG, pointed _ptr at the buffer, put 32767 in _cnt, and called _doprnt --
// leaving _base and _file uninitialised, and relying on _cnt never reaching zero, because
// <stdio.h>'s putc falls through to _flsbuf() when it does and _flsbuf() reads those two
// fields.  This tree has vsnprintf (../libc/stdio/vsnprintf.c) and does not declare _doprnt
// at all, so the whole trick is one call.  vsnprintf and not vsprintf: the buffer is fixed,
// and truncating is exactly the behaviour the 32767 was pretending to.
//
// wprintw() USED TO PASS `&args' where its three siblings pass `args' -- a real bug, not a
// transcription slip, and one that the v7 "address of the last named argument" varargs
// convention made look plausible.  It is gone with the rewrite.
//
#include "internal.h"

// One screen line's worth several times over.  86 words of frame, against a 4,096-word
// user stack at 070000.
#define PRINTW_BUF 512

// The varargs core the four entry points here and in mvprintw.c share.
int _sprintw(WINDOW *win, char *fmt, va_list args)
{
    char buf[PRINTW_BUF];

    vsnprintf(buf, sizeof buf, fmt, args);
    return waddstr(win, buf);
}

int printw(char *fmt, ...)
{
    va_list args;
    int ret;

    va_start(args, fmt);
    ret = _sprintw(stdscr, fmt, args);
    va_end(args);
    return ret;
}

int wprintw(WINDOW *win, char *fmt, ...)
{
    va_list args;
    int ret;

    va_start(args, fmt);
    ret = _sprintw(win, fmt, args);
    va_end(args);
    return ret;
}
