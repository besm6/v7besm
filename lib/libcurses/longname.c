// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// Fill `def' with the long name of the terminal -- the second name in the entry's `|'-
// separated alias list, which is where termcap convention puts the readable one.
//
// THE COPY IS BOUNDED NOW.  v7 wrote until the entry ran out, and the only caller in this
// tree hands it ttytype[50] (cr_tty.c), so an entry with a long second alias would have run
// off the end of a global.  ../libtermcap/termcap.c bounded its own two unbounded strcpy's
// for the same reason; 49 characters is not a guess, it is what the one caller's buffer
// holds.  A caller that wants more must say so by passing a bigger buffer AND changing this
// constant -- there is no way to pass the size through v7's interface, and inventing one
// would break every program that ever called longname().
//
#include "internal.h"

// Room for a name in ttytype[50], the caller's buffer, plus its terminator.
#define LONGNAME_MAX 49

char *longname(const char *bp, char *def)
{
    int n = 0;

    while (*bp && *bp != ':' && *bp != '|')
        bp++;
    if (*bp == '|') {
        bp++;
        while (*bp && *bp != ':' && *bp != '|' && n < LONGNAME_MAX)
            def[n++] = *bp++;
        def[n] = 0;
    }
    return def;
}
