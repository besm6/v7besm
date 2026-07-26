// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// Put the character string cp out, with padding.  The number of affected lines is
// affcnt, and the routine used to output one character is outc.
//
// THERE IS NO PADDING HERE, and that is a decision rather than an omission.  In 4.xBSD
// this routine owns `char PC' and `short ospeed' and emits ceil(delay * speed) pad
// characters after the capability, so that a terminal doing its own cursor motion at
// 300 baud is not overrun by the next byte.  Nothing on this machine can be overrun:
// the console is a Consul-254 driven a character at a time by kernel/dev/sc.c, which
// hands the next one over only when PRP_CONS1_DONE says the last has printed, and the
// SIMH line delivers it as fast as the model runs.  Padding would be dead time.
//
// THE DELAY IS STILL SKIPPED, because it is part of the capability string: `50\E[H'
// means fifty milliseconds and then the escape sequence, and a tputs that did not
// consume the digits would print them.  But it is only stepped over, not converted --
// v7 built a value out of the digits, the fraction and the `*', and there is no longer
// anything here to read it.
//
// The side effect of not padding is that PC and ospeed are not defined here, which
// leaves both names free -- v7's curses defines `char PC;' in curses.c and sets
// `short ospeed' from the sgttyb in cr_tty.c, and would collide with a termlib that
// defined either.  See ../../include/term.h.
//
#include <ctype.h>
#include <term.h>

void tputs(char *cp, int affcnt, int (*outc)(int))
{
    if (cp == 0)
        return;

    // Step over the delay: digits, optionally a decimal point and more digits, optionally
    // a `*' saying the delay scales with the number of lines affected.  v7 accumulated a
    // value out of all that; nothing here would read it, so nothing here builds it.
    while (isdigit(*cp))
        cp++;
    if (*cp == '.') {
        cp++;
        while (isdigit(*cp))
            cp++;
    }
    if (*cp == '*')
        cp++;

    (void)affcnt; // the delay is not computed, so there is nothing to scale

    // The guts of the string.
    while (*cp)
        (*outc)(*cp++);
}
