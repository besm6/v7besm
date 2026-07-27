// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// Fill `def' with the full name of the terminal, taken to be the LAST name in the entry's
// alias list.  Each alias overwrites the one before, so what survives is the last.
//
// Bounded, for longname.c's reason and by the same constant: the caller's buffer is
// ttytype[50] and there is no way to pass a size through this interface.
//
#include "internal.h"

// Room for a name in ttytype[50], the caller's buffer, plus its terminator.
#define FULLNAME_MAX 49

char *fullname(const char *bp, char *def)
{
    int n;

    *def = 0; // in case there is no name at all

    while (*bp && *bp != ':') {
        n = 0;
        while (*bp && *bp != ':' && *bp != '|') {
            if (n < FULLNAME_MAX)
                def[n++] = *bp;
            bp++;
        }
        def[n] = 0;
        if (*bp == '|')
            bp++;
    }
    return def;
}
