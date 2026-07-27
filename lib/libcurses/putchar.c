// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// The one-character output routine the whole library funnels through: _puts() is
// tputs(s, 0, _putchar), cr_put.c's plodput() falls through to it, and refresh.c's makech()
// calls it directly.  Nothing in libcurses writes a byte any other way.
//
// IT IS A SEPARATE FILE SO THAT A PROGRAM CAN REPLACE IT.  b6ld pulls an archive member
// only for a symbol still undefined, so a program that defines its own _putchar never pulls
// this one and gets the entire cursor-motion stream handed to it a byte at a time.
// ../test/cursest.c is built on that: it renders each byte printably, which is what lets one
// plain-ASCII expectation adjudicate a run under b6sim and a run off the disk image.
// Nothing here counts what it wrote -- cr_put.c tracks outcol/outline itself -- so a
// substitute cannot perturb the algorithm.
//
#include "internal.h"

int _putchar(int c)
{
    putchar(c);
    return 0;
}
