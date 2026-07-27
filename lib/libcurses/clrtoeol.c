// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// Clear from the cursor to the end of the line.
//
// A `char *' SCAN BECAME AN int COUNT.  v7 walked `for (sp = maxx; sp < end; sp++)', and a
// relational operator between two `char *' gives the wrong answer here: a fat pointer keeps
// its byte offset in bits 47-45 and its word address in bits 15-1, and the offset DECREMENTS
// as the pointer advances, so the offset field dominates and the ordering comes out
// scrambled and inverted within every word.  (Subtraction is fine -- b$pdiff decodes both
// operands -- so `p - base' stays as v7 wrote it wherever it appears.  See
// ../libtermcap/README.md and ../../doc/Besm6_Data_Representation.md.)
//
// The scan is a memset now rather than an indexed loop, because the only thing v7's
// conditional store bought was a `minx'/`maxx' pair that fed a debug fprintf: touchline() is
// called below with the whole tail of the line whether or not anything changed.  With the
// DEBUG scaffolding gone (see internal.h) there is no reader left, and blanking a cell that
// is already blank costs nothing.
//
#include "internal.h"
#include <string.h>

void wclrtoeol(WINDOW *win)
{
    int y, x;

    y = win->_cury;
    x = win->_curx;
    memset(&win->_y[y][x], ' ', win->_maxx - x);

    // Update firstch and lastch for the line.
    touchline(win, y, win->_curx, win->_maxx - 1);
}
