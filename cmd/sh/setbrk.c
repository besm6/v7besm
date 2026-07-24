/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 1999 Robert Nordier. All rights reserved. */

//
// Move the program break, and record where it now reaches.
//
// v7 returned the old break cast to an int, and its one caller that looked (fault.c, on
// a memory fault) tested the result against -1.  Neither works here.  This tree's
// sbrk() REPORTS FAILURE AS NULL, not as (char *)-1, because -1 would mean fabricating
// a fat pointer out of an integer -- the bit-48 marker and the byte offset would both
// be wrong (lib/libc/sys/sbrk.c says so at length).  And casting the successful pointer
// back to an int, which is all v7's callers ever did with it, is the same pun in the
// other direction.
//
// So this returns a plain success flag instead, and nobody has to know what a break
// looks like.  `incr' is still in char-units: sbrk() takes bytes and converts to whole
// words itself, rounding growth up and shrinkage toward zero.
//
#include <unistd.h>

#include "defs.h"

INT setbrk(INT incr)
{
    BYTPTR a = sbrk(incr);

    if (a == 0)
        return 0;
    brkend = a + incr;
    return 1;
}
