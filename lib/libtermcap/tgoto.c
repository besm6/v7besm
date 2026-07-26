// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// Routine to perform cursor addressing.  CM is a string containing printf type escapes
// to allow cursor addressing.  We start out ready to print the destination line, and
// switch each time we print row or column.  The following escapes are defined for
// substituting row/column:
//
//      %d      as in printf
//      %2      like %2d
//      %3      like %3d
//      %.      gives %c hacking special case characters
//      %+x     like %c but adding x first
//
//      The codes below affect the state but don't use up a value.
//
//      %>xy    if value > x add y
//      %r      reverses row/column
//      %i      increments row/column (for one origin indexing)
//      %%      gives %
//      %B      BCD (2 decimal digits encoded in one byte)
//      %D      Delta Data (backwards bcd)
//
// all other characters are ``self-inserting''.
//
// NOTHING HERE COMPARES TWO char * VALUES, so unlike termcap.c this file is v7's with
// only the C11 pass applied -- see that file's header for why a comparison would matter.
// The four conditional forms (%n, %>, %B, %D) that the upstream Makefile switched on
// with -DCM_N -DCM_GT -DCM_B -DCM_D are compiled UNCONDITIONALLY here: they are a few
// words each, `tgoto' is one archive member either way, and a database entry that uses
// one and finds it missing degrades to the string "OOPS" on the screen rather than to a
// diagnostic.
//
#include <string.h>
#include <term.h>

#define MAXRETURNSIZE 64

char *tgoto(char *CM, int destcol, int destline)
{
    static char result[MAXRETURNSIZE];
    static char added[10];
    char *cp          = CM;
    register char *dp = result;
    register int c;
    int oncol          = 0;
    register int which = destline;

    if (cp == 0) {
    toohard:
        // ``We don't do that under BOZO's big top''
        return ("OOPS");
    }
    added[0] = 0;
    while ((c = *cp++)) {
        if (c != '%') {
            *dp++ = c;
            continue;
        }
        switch (c = *cp++) {
        case 'n':
            destcol ^= 0140;
            destline ^= 0140;
            goto setwhich;

        case 'd':
            if (which < 10)
                goto one;
            if (which < 100)
                goto two;
            // fall into...

        case '3':
            *dp++ = (which / 100) | '0';
            which %= 100;
            // fall into...

        case '2':
        two:
            *dp++ = which / 10 | '0';
        one:
            *dp++ = which % 10 | '0';
        swap:
            oncol = 1 - oncol;
        setwhich:
            which = oncol ? destcol : destline;
            continue;

        case '>':
            if (which > *cp++)
                which += *cp++;
            else
                cp++;
            continue;

        case '+':
            which += *cp++;
            // fall into...

        case '.':
            *dp++ = which;
            goto swap;

        case 'r':
            oncol = 1;
            goto setwhich;

        case 'i':
            destcol++;
            destline++;
            which++;
            continue;

        case '%':
            *dp++ = c;
            continue;

        case 'B':
            which = (which / 10 << 4) + which % 10;
            continue;

        case 'D':
            which = which - 2 * (which % 16);
            continue;

        default:
            goto toohard;
        }
    }
    strcpy(dp, added);
    return (result);
}
