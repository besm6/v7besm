/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 1999 Robert Nordier. All rights reserved. */

//
// Character handling for command lines: the shell's lexer.
//
// QUOTING RIDES IN BIT 0200 of each character, and that works here for the reason it
// worked on the PDP-11 even though `char' is signed there and unsigned here: every test
// in ctype.h checks (c & QUOTE) == 0 first and short-circuits, so a marked character is
// never used as a table subscript.  What changes is only the sign of the intermediate,
// which nothing looks at.
//
#include <unistd.h>

#include "defs.h"
#include "sym.h"

static INT readb(void);

INT word(void)
{
    CHAR c, d;
    CHAR *argp = locstak() + BYTESPERWORD;
    INT alpha  = 1;

    wdnum = 0;
    wdset = 0;

    while (c = nextc(0), space(c))
        ;
    if (!eofmeta(c)) {
        do {
            chkstak(argp);
            if (c == LITERAL) {
                *argp++ = DQUOTE;
                while ((c = readc()) && c != LITERAL) {
                    chkstak(argp);
                    *argp++ = c | QUOTE;
                    chkpr(c);
                }
                chkstak(argp);
                *argp++ = DQUOTE;
            } else {
                *argp++ = c;
                if (c == '=')
                    wdset |= alpha;
                if (!alphanum(c))
                    alpha = 0;
                if (qotchar(c)) {
                    d = c;
                    do {
                        chkstak(argp);
                    } while ((*argp++ = (c = nextc(d))) && c != d && (chkpr(c), 1));
                }
            }
        } while (c = nextc(0), !eofmeta(c));
        argp = endstak(argp);
        if (!letter(((ARGPTR)argp)->argval[0]))
            wdset = 0;

        peekc = c | MARK;
        if (((ARGPTR)argp)->argval[1] == 0 && (d = ((ARGPTR)argp)->argval[0], digit(d)) &&
            (c == '>' || c == '<')) {
            word();
            wdnum = d - '0';
        } else {
            // check for reserved words
            if (reserv == FALSE || (wdval = syslook(((ARGPTR)argp)->argval, reserved)) == 0) {
                wdarg = (ARGPTR)argp;
                wdval = 0;
            }
        }

    } else if (dipchar(c)) {
        if ((d = nextc(0)) == c) {
            wdval = c | SYMREP;
        } else {
            peekc = d | MARK;
            wdval = c;
        }
    } else {
        if ((wdval = c) == SHEOF)
            wdval = EOFSYM;
        if (iopend && eolchar(c)) {
            copy(iopend);
            iopend = 0;
        }
    }
    reserv = FALSE;
    return wdval;
}

INT nextc(CHAR quote)
{
    CHAR c, d;

    if ((d = readc()) == ESCAPE) {
        if ((c = readc()) == NL) {
            chkpr(NL);
            d = nextc(quote);
        } else if (quote && c != quote && !escchar(c)) {
            peekc = c | MARK;
        } else {
            d = c | QUOTE;
        }
    }
    return d;
}

INT readc(void)
{
    CHAR c;
    INT len;
    SHFILE f;

retry:
    if (peekc) {
        c     = peekc;
        peekc = 0;
    } else if (f = standin, f->fnxt != f->fend) {
        if ((c = *f->fnxt++) == 0) {
            if (f->feval) {
                if (estabf(*f->feval++))
                    c = SHEOF;
                else
                    c = SP;
            } else {
                goto retry; // = c=readc();
            }
        }
        if (flags & readpr && standin->fstak == 0)
            prc(c);
        if (c == NL)
            f->flin++;
    } else if (f->feof || f->fdes < 0) {
        c = SHEOF;
        f->feof++;
    } else if ((len = readb()) <= 0) {
        close(f->fdes);
        f->fdes = -1;
        c       = SHEOF;
        f->feof++;
    } else {
        f->fend = (f->fnxt = f->fbuf) + len;
        goto retry;
    }
    return c;
}

static INT readb(void)
{
    SHFILE f = standin;
    INT len;

    do {
        if (trapnote & SIGSET) {
            newline();
            sigchk();
        }
    } while ((len = read(f->fdes, f->fbuf, f->fsiz)) < 0 && trapnote);
    return len;
}
