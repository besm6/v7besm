// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// Re-aim every subwindow's _y pointers at the parent's rows.
//
// A subwindow does not own its characters: _set_subwin_() points each of its _y[] entries
// into the parent's corresponding row (newwin.c).  wdeleteln() and winsertln() on the
// parent ROTATE those row pointers rather than move characters, so after either of them
// every subwindow at or below the affected line is aiming at the wrong row and has to be
// re-pointed.  Subwindows that end above the parent's cursor are untouched.
//
// THE STARTING ROW IS CLAMPED, AND 4.3BSD DID NOT CLAMP IT.  `realy - win->_begy' is the
// subwindow row holding the parent's cursor line, and it is NEGATIVE for a subwindow that
// begins BELOW that line -- which the guard above does not exclude, since that guard only
// drops subwindows lying entirely above it.  v7 then wrote win->_y[-1], one word in front
// of the malloc'd row-pointer array, which is the block header: after a wdeleteln() on a
// parent whose cursor is above its subwindow, the next free() walks a corrupt arena and
// does not come back.  Caught by ../test/cursest.c, whose windows() does exactly that.
//
// When the subwindow starts below the cursor, its row 0 corresponds to parent row
// win->_begy - orig->_begy, which is at or below orig->_cury -- so the parent-side index
// moves down by as much as the subwindow-side one moves up.
//
#include "internal.h"

void _id_subwins(WINDOW *orig)
{
    WINDOW *win;
    int realy;
    int y, oy;

    realy = orig->_begy + orig->_cury;
    for (win = orig->_nextp; win != orig; win = win->_nextp) {
        // If the window ends before our current position, nothing to do.
        if (win->_begy + win->_maxy <= realy)
            continue;

        oy = orig->_cury;
        y  = realy - win->_begy;
        if (y < 0) {
            oy -= y; // the subwindow begins below the cursor line
            y = 0;
        }
        for (; y < win->_maxy && oy < orig->_maxy; y++, oy++)
            win->_y[y] = &orig->_y[oy][win->_ch_off];
    }
}
