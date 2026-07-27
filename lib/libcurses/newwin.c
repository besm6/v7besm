// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// Allocate a window and set up its defaults.
//
// A ROW IS malloc(nc), NOT malloc(nc * sizeof win->_y[0]).  4.3BSD wrote the latter, and
// _y[0] is a `char *' -- six char-units on this machine -- so every row of every window was
// six times the size it needed to be.  For a 24x80 stdscr and curscr that is 3,168 wasted
// words: 11% of the 28,672-word user address space, and the difference between a one-page
// heap and a four-page one.  The other three allocations in this file are correct as
// written and must not be "fixed" alongside it: _y itself IS an array of `char *', and
// _firstch/_lastch ARE arrays of `short'.
//
// makenew() INITIALISES _orig, _nextp AND _ch_off, which 4.3BSD left to newwin() and
// subwin() -- and _orig it left to nobody.  malloc does not zero, and _orig is the flag that
// decides whether a window owns its rows: delwin(), wdeleteln() and winsertln() all branch
// on it, and a top-level window that came back with garbage there takes the subwindow path,
// copying characters where it should rotate row pointers and freeing none of its rows.
// Latent on most runs only because a fresh sbrk page happens to be zero.
//
#include "internal.h"
#include <stdlib.h>
#include <string.h>

#define SMALLOC (short *)malloc

#undef nl // not needed here, and it collides with the local variable

// Allocate the window itself and its row-pointer array, and fill in everything that does
// not depend on which of newwin()/subwin() is asking.
static WINDOW *makenew(int num_lines, int num_cols, int begy, int begx)
{
    WINDOW *win;
    int by, bx, nl, nc;

    by = begy;
    bx = begx;
    nl = num_lines;
    nc = num_cols;

    if ((win = (WINDOW *)malloc(sizeof *win)) == NULL)
        return NULL;
    if ((win->_y = (char **)malloc(nl * sizeof win->_y[0])) == NULL) {
        free(win);
        return NULL;
    }
    win->_cury = win->_curx = 0;
    win->_clear             = FALSE;
    win->_maxy              = nl;
    win->_maxx              = nc;
    win->_begy              = by;
    win->_begx              = bx;
    win->_flags             = 0;
    win->_scroll = win->_leave = FALSE;
    // A window is its own ring and owns its rows until subwin() says otherwise.  See the
    // header comment: leaving _orig alone here is what 4.3BSD did, and it is a real bug.
    win->_nextp  = win;
    win->_orig   = NULL;
    win->_ch_off = 0;
    _swflags_(win);
    return win;
}

WINDOW *newwin(int num_lines, int num_cols, int begy, int begx)
{
    WINDOW *win;
    int i, j, by, bx, nl, nc;

    by = begy;
    bx = begx;
    nl = num_lines;
    nc = num_cols;

    if (nl == 0)
        nl = LINES - by;
    if (nc == 0)
        nc = COLS - bx;
    if ((win = makenew(nl, nc, by, bx)) == NULL)
        return NULL;
    if ((win->_firstch = SMALLOC(nl * sizeof win->_firstch[0])) == NULL) {
        free(win->_y);
        free(win);
        return NULL;
    }
    if ((win->_lastch = SMALLOC(nl * sizeof win->_lastch[0])) == NULL) {
        free(win->_y);
        free(win->_firstch);
        free(win);
        return NULL;
    }
    for (i = 0; i < nl; i++) {
        win->_firstch[i] = _NOCHANGE;
        win->_lastch[i]  = _NOCHANGE;
    }
    for (i = 0; i < nl; i++)
        if ((win->_y[i] = malloc(nc)) == NULL) {
            for (j = 0; j < i; j++)
                free(win->_y[j]);
            free(win->_firstch);
            free(win->_lastch);
            free(win->_y);
            free(win);
            return NULL;
        } else
            // v7 filled the row with a `char *' scan (`for (sp = _y[i]; sp < _y[i]+nc; )'),
            // which is the comparison clrtoeol.c's header explains cannot be trusted here.
            memset(win->_y[i], ' ', nc);
    return win;
}

WINDOW *subwin(WINDOW *orig, int num_lines, int num_cols, int begy, int begx)
{
    WINDOW *win;
    int by, bx, nl, nc;

    by = begy;
    bx = begx;
    nl = num_lines;
    nc = num_cols;

    // Make sure the window fits inside the original one.
    if (by < orig->_begy || bx < orig->_begx || by + nl > orig->_maxy + orig->_begy ||
        bx + nc > orig->_maxx + orig->_begx)
        return NULL;
    if (nl == 0)
        nl = orig->_maxy + orig->_begy - by;
    if (nc == 0)
        nc = orig->_maxx + orig->_begx - bx;
    if ((win = makenew(nl, nc, by, bx)) == NULL)
        return NULL;
    win->_nextp  = orig->_nextp;
    orig->_nextp = win;
    win->_orig   = orig;
    _set_subwin_(orig, win);
    return win;
}

// Point a subwindow's rows and change vectors into its parent's.  Shared with mvwin().
void _set_subwin_(WINDOW *orig, WINDOW *win)
{
    register int i, j, k;

    j            = win->_begy - orig->_begy;
    k            = win->_begx - orig->_begx;
    win->_ch_off = k;
    win->_firstch = &orig->_firstch[j];
    win->_lastch  = &orig->_lastch[j];
    for (i = 0; i < win->_maxy; i++, j++)
        win->_y[i] = &orig->_y[j][k];
}

// Recompute the geometry flags: whether the window reaches the right edge, spans a whole
// line, fills the screen, or touches the bottom.
void _swflags_(WINDOW *win)
{
    win->_flags &= ~(_ENDLINE | _FULLLINE | _FULLWIN | _SCROLLWIN);
    if (win->_begx + win->_maxx == COLS) {
        win->_flags |= _ENDLINE;
        if (win->_begx == 0) {
            if (AL && DL)
                win->_flags |= _FULLLINE;
            if (win->_maxy == LINES && win->_begy == 0)
                win->_flags |= _FULLWIN;
        }
        if (win->_begy + win->_maxy == LINES)
            win->_flags |= _SCROLLWIN;
    }
}
