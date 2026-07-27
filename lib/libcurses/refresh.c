// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// Make the current screen look like `win' over the area win covers.
//
// THIS FILE HELD TWO OF THE ELEVEN `char *' COMPARISONS, and they are the two that matter
// most: both decide how much of a line is trailing blanks, which is what the clear-to-end-
// of-line optimisation is built on.  A relational operator between two `char *' gives the
// wrong answer here (clrtoeol.c's header has the arithmetic), and neither of these faults
// when it is wrong -- one truncates the new line's blank run, the other the screen's, and
// the result is a `CE' emitted where sixty spaces were needed or sixty spaces where a `CE'
// would have done.  Nothing but comparing real output catches that.
//
// BOTH CURSORS ARE GONE, NOT JUST THE COMPARISONS.  `nsp' and `csp' walked in lockstep with
// `wx' -- nsp is always &win->_y[wy][wx] and csp always &curscr->_y[y][wx + _begx] -- so
// indexing off a row base says the same thing and leaves nothing to compare.  The one place
// that is not a mechanical substitution is `csp = " "' when refreshing curscr itself: the
// old code read a one-character literal over and over, which is just "the screen is blank
// here", so cur() answers ' ' in that case and there is no second row at all.
//
// That also settles a comparison that LOOKS cross-object and is not.  `if (ce-- <= csp)'
// pitted a pointer into curscr's row against `csp', which is the literal `" "' when curwin
// -- but that arm is unreachable: `ce' is NULL exactly when curwin, and the block is guarded
// by `ce != NULL'.  Same allocation, always.  It still had to go.
//
#include "internal.h"
#include <string.h>

static short ly, lx;

static int curwin;

WINDOW *_win = NULL;

// Perform a mvcur, leaving standout mode first if the terminal cannot move while in it.
static void domvcur(int oy, int ox, int ny, int nx)
{
    if (curscr->_flags & _STANDOUT && !MS) {
        _puts(SE);
        curscr->_flags &= ~_STANDOUT;
    }
    mvcur(oy, ox, ny, nx);
}

// Make one line of the screen match one line of the window.
static int makech(WINDOW *win, short wy)
{
    char *nrow, *crow;
    short wx, lch, y;
    int i, nlsp = 0, clsp;
    int begx = win->_begx;

    wx = win->_firstch[wy] - win->_ch_off;
    if (wx >= win->_maxx)
        return OK;
    else if (wx < 0)
        wx = 0;
    lch = win->_lastch[wy] - win->_ch_off;
    if (lch < 0)
        return OK;
    else if (lch >= win->_maxx)
        lch = win->_maxx - 1;
    y = wy + win->_begy;

    nrow = win->_y[wy];
    crow = curwin ? NULL : curscr->_y[y];

    // What the screen holds at window column wx.  Refreshing curscr against itself means
    // there is nothing to compare against, and v7 answered a blank; so do we.
#define cur(wx) (curwin ? ' ' : crow[(wx) + begx])

    // The window line's last non-blank column, for the clear-to-end-of-line decision below.
    if (CE && !curwin) {
        for (i = win->_maxx - 1; nrow[i] == ' '; i--)
            if (i <= 0)
                break;
        nlsp = i;
    }

    // From here `ce' is the CE capability, or NULL if it must not be used.  (v7 reused the
    // same variable as a cursor two blocks down, which is what made the second comparison
    // hard to see.)
    char *ce = curwin ? NULL : CE;

    while (wx <= lch) {
        if (nrow[wx] != cur(wx)) {
            domvcur(ly, lx, y, wx + begx);
            ly = y;
            lx = wx + begx;
            while (nrow[wx] != cur(wx) && wx <= lch) {
                if (ce != NULL && wx >= nlsp && nrow[wx] == ' ') {
                    // Would clearing to end of line be cheaper than printing the blanks?
                    // Find where the screen's own trailing blank run starts.
                    // Transcribed exactly from v7's `while (*ce == ' ') if (ce-- <= csp)
                    // break;' -- the test uses the pre-decrement value, so the floor is
                    // checked before the step and the scan can end one below it.
                    int ci = wx + begx;
                    i      = COLS - 1;
                    while (crow[i] == ' ') {
                        int atfloor = (i <= ci);
                        i--;
                        if (atfloor)
                            break;
                    }
                    clsp = i - begx;
                    if (clsp - nlsp >= strlen(CE) && clsp < win->_maxx) {
                        _puts(CE);
                        lx = wx + begx;
                        for (i = wx; i <= clsp; i++)
                            crow[i + begx] = ' ';
                        return OK;
                    }
                    ce = NULL;
                }
                // Enter or leave standout mode as the cell requires.
                if (SO && (nrow[wx] & _STANDOUT) != (curscr->_flags & _STANDOUT)) {
                    if (nrow[wx] & _STANDOUT) {
                        _puts(SO);
                        curscr->_flags |= _STANDOUT;
                    } else {
                        _puts(SE);
                        curscr->_flags &= ~_STANDOUT;
                    }
                }
                wx++;
                if (wx >= win->_maxx && wy == win->_maxy - 1) {
                    if (win->_scroll) {
                        if ((curscr->_flags & _STANDOUT) && (win->_flags & _ENDLINE))
                            if (!MS) {
                                _puts(SE);
                                curscr->_flags &= ~_STANDOUT;
                            }
                        if (!curwin)
                            crow[wx - 1 + begx] = nrow[wx - 1];
                        _putchar(nrow[wx - 1] & 0177);
                        if (win->_flags & _FULLWIN && !curwin)
                            scroll(curscr);
                        ly = win->_begy + win->_cury;
                        lx = begx + win->_curx;
                        return OK;
                    } else if (win->_flags & _SCROLLWIN) {
                        lx = --wx;
                        return ERR;
                    }
                }
                if (!curwin)
                    crow[wx - 1 + begx] = nrow[wx - 1];
                _putchar(nrow[wx - 1] & 0177);
                if (UC && (nrow[wx - 1] & _STANDOUT)) {
                    _putchar('\b');
                    _puts(UC);
                }
            }
            if (lx == wx + begx) // no change
                break;
            lx = wx + begx;
            if (lx >= COLS && AM) {
                lx = 0;
                ly++;
                // xn glitch: the terminal chomps a newline after an auto-wrap.  Feed it one
                // now and forget about it.
                if (XN) {
                    _putchar('\n');
                    _putchar('\r');
                }
            }
        } else if (wx <= lch)
            while (nrow[wx] == cur(wx) && wx <= lch)
                ++wx;
        else
            break;
    }
    return OK;
#undef cur
}

int wrefresh(WINDOW *win)
{
    short wy;
    int retval;

    // Make sure we are in visual state.
    if (_endwin) {
        _puts(VS);
        _puts(TI);
        _endwin = FALSE;
    }

    ly     = curscr->_cury;
    lx     = curscr->_curx;
    wy     = 0;
    _win   = win;
    curwin = (win == curscr);

    if (win->_clear || curscr->_clear || curwin) {
        if ((win->_flags & _FULLWIN) || curscr->_clear) {
            _puts(CL);
            ly = 0;
            lx = 0;
            if (!curwin) {
                curscr->_clear = FALSE;
                curscr->_cury  = 0;
                curscr->_curx  = 0;
                werase(curscr);
            }
            touchwin(win);
        }
        win->_clear = FALSE;
    }
    if (!CA) {
        if (win->_curx != 0)
            _putchar('\n');
        if (!curwin)
            werase(curscr);
    }
    for (wy = 0; wy < win->_maxy; wy++) {
        if (win->_firstch[wy] != _NOCHANGE) {
            if (makech(win, wy) == ERR)
                return ERR;
            else {
                if (win->_firstch[wy] >= win->_ch_off)
                    win->_firstch[wy] = win->_maxx + win->_ch_off;
                if (win->_lastch[wy] < win->_maxx + win->_ch_off)
                    win->_lastch[wy] = win->_ch_off;
                if (win->_lastch[wy] < win->_firstch[wy])
                    win->_firstch[wy] = _NOCHANGE;
            }
        }
    }

    if (win == curscr)
        domvcur(ly, lx, win->_cury, win->_curx);
    else {
        if (win->_leave) {
            curscr->_cury = ly;
            curscr->_curx = lx;
            ly -= win->_begy;
            lx -= win->_begx;
            if (ly >= 0 && ly < win->_maxy && lx >= 0 && lx < win->_maxx) {
                win->_cury = ly;
                win->_curx = lx;
            } else
                win->_cury = win->_curx = 0;
        } else {
            domvcur(ly, lx, win->_cury + win->_begy, win->_curx + win->_begx);
            curscr->_cury = win->_cury + win->_begy;
            curscr->_curx = win->_curx + win->_begx;
        }
    }
    retval = OK;
    _win   = NULL;
    fflush(stdout);
    return retval;
}
