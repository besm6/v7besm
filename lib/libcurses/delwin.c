// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// Delete a window and give its space back.
//
// WHICH BRANCH IS TAKEN TURNS ENTIRELY ON _orig, so newwin.c's makenew() must initialise it
// -- and in 4.3BSD it did not.  A top-level window whose _orig held whatever malloc last
// left there would walk the else-branch below, looking for itself in a _nextp ring it is
// the only member of, and free none of its rows.  See makenew() in newwin.c.
//
#include "internal.h"
#include <stdlib.h>

int delwin(WINDOW *win)
{
    int i;
    WINDOW *wp, *np;

    if (win->_orig == NULL) {
        // The original window: free the rows, the change vectors, and every subwindow
        // hanging off the ring.
        for (i = 0; i < win->_maxy && win->_y[i]; i++)
            free(win->_y[i]);
        free(win->_firstch);
        free(win->_lastch);
        wp = win->_nextp;
        while (wp != win) {
            np = wp->_nextp;
            delwin(wp);
            wp = np;
        }
    } else {
        // A subwindow: take ourselves out of the ring.  The minimum ring is the original
        // followed by this subwindow, so there are always at least two members.
        for (wp = win->_nextp; wp->_nextp != win; wp = wp->_nextp)
            continue;
        wp->_nextp = win->_nextp;
    }
    free(win->_y);
    free(win);
    return 0;
}
