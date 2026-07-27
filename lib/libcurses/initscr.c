// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// Initialise the current and standard screens.
//
// TWO PATHS, AND WHICH ONE IS TAKEN MATTERS MORE HERE THAN IT DID ON A VAX.  With My_term
// set the caller has named the terminal in Def_term and this routine asks the tty nothing;
// otherwise it hunts for a descriptor that is a tty, reads the modes through gettmode(), and
// consults $TERM.  Under the a.out simulator ioctl is an unconditional no-op and gtty
// zero-fills (cmd/sim/syscall.cpp), while under the booted kernel the console really is
// ECHO|CRMOD|XTABS (kernel/dev/sc.c) -- so GT, NONL and _pfast come out differently in the
// two worlds, and those three change the emitted cursor motion.  ../test/cursest.c sets
// My_term for exactly that reason; ../test/curstty.c is the test that does NOT, and runs on
// the disk image only.
//
// getdtablesize() IS GONE.  There is no such call here, and the honest bound is a constant:
// the descriptor curses wants is a terminal the program did not open itself, which means one
// of the three standard ones.  If none of them is a tty, fall back to the default of 1
// rather than leave _tty_ch past the end of the search -- v7 left it at nfd, so every later
// ioctl failed on a descriptor that was never open.
//
// The SIGTSTP handler 4.2BSD installed here is gone with tstp.c: this system has no job
// control (../../include/signal.h stops at fifteen signals and NSIG 17).
//
#include "internal.h"
#include <stdlib.h>
#include <unistd.h>

// The standard descriptors, which is where a terminal curses may use will be.
#define NSTDFD 3

WINDOW *initscr(void)
{
    char *sp;

    if (My_term)
        setterm(Def_term);
    else {
        for (_tty_ch = 0; _tty_ch < NSTDFD; _tty_ch++)
            if (isatty(_tty_ch))
                break;
        if (_tty_ch >= NSTDFD)
            _tty_ch = 1; // no terminal among them; leave it where curses.c had it
        gettmode();
        sp = getenv("TERM");
        if (!sp)
            sp = Def_term;
        setterm(sp);
    }
    _puts(TI);
    _puts(VS);
    if (curscr != NULL)
        delwin(curscr);
    if ((curscr = newwin(LINES, COLS, 0, 0)) == NULL)
        return NULL;
    clearok(curscr, TRUE);
    curscr->_flags &= ~_FULLLINE;
    if (stdscr != NULL)
        delwin(stdscr);
    stdscr = newwin(LINES, COLS, 0, 0);
    return stdscr;
}
