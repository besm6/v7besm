/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 1999 Robert Nordier. All rights reserved. */

//
// Character handling for command lines: the shell's lexer.
//
// QUOTING DOES NOT RIDE IN BIT 0200 here, and defs.h is where that is set out.  v7 marked
// a quoted character by setting the top bit of it, which costs the eighth bit of every
// byte the shell handles -- and this machine's text is UTF-8, so that bit is data.  A
// character in flight carries the mark in a bit far above the byte; a character on the
// stack carries it as a QESC byte in front.  This file is where both are produced:
// nextc() puts the in-flight mark on, and word() calls pushq() to lay the stored form
// down.
//
// It is also where the stored form is READ back, because macro() pushes a word as an
// input and re-reads it through readc().  That is what the fencd flag is for.
//
#include <unistd.h>

#include "defs.h"
#include "sym.h"

// Defined below.
static INT readb(void);

//
// Read the next word or operator from the input -- the shell's lexer.
//
// An ordinary word is built up on the expression stack a character at a time, until a
// character arrives that cannot be part of one (a space, a newline, an operator).  It is
// left in `wdarg' and 0 is returned.  Anything else -- ; & | < > ( ) and the reserved
// words -- returns a symbol number instead, and the parser in cmd.c switches on it.
//
// Two jobs are done on the way past.  Quoting is resolved here, not later: a quoted
// character gets bit 0200 set so that the rest of the shell knows not to treat it as a
// wildcard or a word separator.  And a word that looks like `name=value' sets `wdset',
// so the parser can tell an assignment from an argument.
//
// A third, which v7 had not got: a COMMENT is discarded here.  See COMCHAR below.
//
INT word(void)
{
    INT c, d;
    CHAR *argp = locstak() + BYTESPERWORD;
    INT alpha  = 1;

    wdnum = 0;
    wdset = 0;

    while (c = nextc(0), space(c))
        ;
    //
    // A `#' WHERE A WORD WOULD START begins a comment, which runs to end of line.  This is
    // the one place it can be tested and the only place it needs to be: a `#' anywhere else
    // has already been taken by the loop below (so `echo a#b' is literal), by the quoting
    // arms, or by nextc()'s backslash (which returns it as 0243, never as '#').  Nothing
    // in the character tables changes, and $# is untouched -- macro.c reads that one.
    //
    // Three things here are contracts, not taste:
    //  - readc(), not nextc(): nextc() eats a `\'-newline as a line continuation and would
    //    pull the next line into the comment;
    //  - stop on SHEOF as well as NL: readc() returns SHEOF forever once the input is
    //    spent, so a comment on an unterminated last line would spin;
    //  - FALL THROUGH with the newline in hand rather than looping back to the space skip.
    //    That is what makes a comment line indistinguishable from an empty one below --
    //    including the pending here-document flush, which fires on the newline.
    //
    if (c == COMCHAR) {
        while ((c = readc()) != NL && c != SHEOF)
            ;
    }
    if (!eofmeta(c)) {
        do {
            chkstak(argp);
            if (c == LITERAL) {
                *argp++ = DQUOTE;
                while ((c = readc()) && c != LITERAL) {
                    argp = putq(argp, c | QUOTE);
                    chkpr(c);
                }
                chkstak(argp);
                *argp++ = DQUOTE;
            } else {
                argp = putq(argp, c);
                if (c == '=')
                    wdset |= alpha;
                if (!alphanum(c))
                    alpha = 0;
                if (qotchar(c)) {
                    //
                    // Inside "..." or `...` the quotes THEMSELVES are copied and macro.c
                    // marks what is between them later, when it re-reads the word; only a
                    // backslash escape is marked here, by nextc().  Either way putq() is
                    // what lays the character down, so a literal QESC in the text is
                    // doubled and cannot be read back as a mark.
                    //
                    d = c;
                    while ((c = nextc(d)) != 0) {
                        argp = putq(argp, c);
                        if (c == d)
                            break;
                        chkpr(c);
                    }
                    if (c == 0) {
                        chkstak(argp);
                        *argp++ = 0;
                    }
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

//
// Read the next character, dealing with a backslash in front of it.
//
// Backslash-newline vanishes and the line continues.  Backslash-anything-else marks the
// character as quoted.  `quote' is the quote character currently open, if any, since a
// backslash means less inside double quotes than outside them.
//
INT nextc(INT quote)
{
    INT c, d;

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

//
// Read one BYTE of input, with no interpretation at all.
//
// It takes from the current input buffer, and failing that from a fresh read of the file.
// At the end of an input it reports end of file, which is what lets `.' and `eval' finish
// by simply running out.
//
static INT readraw(void)
{
    INT c;
    INT len;
    SHFILE f;

retry:
    if (f = standin, f->fnxt != f->fend) {
        if ((c = *f->fnxt++) == 0) {
            if (f->feval) {
                if (estabf(*f->feval++))
                    c = SHEOF;
                else
                    c = SP;
            } else {
                goto retry; // = c=readraw();
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

//
// Read the next character of input.  Everything the shell reads comes through here.
//
// It takes from the one-character pushback the lexer left, and failing that from readraw()
// above.
//
// IT IS ALSO THE ONE DECODER OF THE STORED FORM, on the two inputs that carry it (mode.h:
// fencd).  That is one site rather than three: skipto() and comsubst()'s collector read
// with readc() and no interpretation of their own, and if the decoding lived in getch()
// instead then a quoted `}' would close a ${...} and a quoted backquote would truncate a
// command substitution.
//
// TWO THINGS HERE ARE CONTRACTS, not tidiness:
//
//   - THE PAYLOAD IS FETCHED WITH readraw() AND NOT WITH readc().  A payload is a byte and
//     nothing else; fetching it through the decoder makes `QESC QESC' -- a literal 0377 --
//     eat the character after it instead of standing for itself.  readraw() is also why
//     the pair may straddle a buffer boundary, which it can: a here-document temp file is
//     read SHBUFSIZ bytes at a time.
//   - THE PUSHBACK IS RETURNED BEFORE THE TEST, not after.  peekc holds a character that
//     has already been through here once, so a pushed-back literal 0377 must not be read
//     as a mark a second time.  (v7 dropped MARK by truncating into a CHAR; the quote bit
//     lives above the byte now, so the mask is written out.)
//
INT readc(void)
{
    INT c;

    if (peekc) {
        c     = peekc & ~MARK;
        peekc = 0;
        return c;
    }
    c = readraw();
    if (c == QESC && standin->fencd) {
        c = readraw();
        if (c == 0)
            return 0; // a lone QESC at the end: the empty quoted word
        if (c != QESC)
            c |= QUOTE; // QESC QESC is a literal 0377, not a quoted character
    }
    return c;
}

//
// Refill the input buffer.  A read interrupted by a signal is retried rather than
// treated as end of file, which is what lets a trap fire while the shell sits waiting
// for the user to type something.
//
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
