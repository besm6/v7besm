// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// termcap -- routines for dealing with the terminal capability data base.
//
// Essentially all the work here is scanning and decoding escapes in string
// capabilities.  We don't use stdio because the editor doesn't, and because living
// w/o it is not hard.  (Which is worth more on this machine than it was on a PDP-11:
// a FILE is words this address space would rather spend on the entry itself.)
//
// BUG:   Should use a "last" pointer in tbuf, so that searching for capabilities
//        alphabetically would not be a n**2/2 process when large numbers of
//        capabilities are given.
// Note:  If we add a last pointer now we will screw up the tc capability.  We really
//        should compile termcap.
//
// WHAT THE BESM-6 PORT CHANGED.  Three things, and the first is the one to read:
//
//   * FOUR `char *' COMPARISONS ARE GONE.  A relational operator between two char *
//     values gives the wrong answer here.  A fat pointer carries its byte offset in
//     bits 47-45 and its word address in bits 15-1, and the OFFSET DECREMENTS as the
//     pointer advances (../../doc/Besm6_Data_Representation.md); there is no
//     relational helper, so `<' compiles to an integer comparison of the whole word,
//     the offset field dominates the address field, and the ordering comes out
//     scrambled and inverted within each word.  `p < end' on a buffer cursor is
//     silently, unpredictably wrong -- see ../../cmd/ls/README.md, which met the same
//     hazard in makename().  Every one of them is an explicit int count now.
//     SUBTRACTION IS FINE (b$pdiff decodes both operands), so the `p - holdtbuf'
//     expressions below stand as v7 wrote them, modulo the base -- see tnchktc().
//
//   * MAXHOP IS 4, not 32.  tgetent() holds ibuf[TBUFSIZ] and tnchktc() holds
//     tcbuf[TBUFSIZ], and the two recurse into each other -- so at six chars to the
//     word a `tc=' hop costs 2 * 1024/6 = 342 words of stack plus two frames, call it
//     360.  The user stack is FOUR PAGES, 4096 words, at 070000 (../../CLAUDE.md), so
//     v7's 32 hops would want 11,500 words and blow it in silence.  Four is past any
//     real database: the /etc/termcap this system ships has exactly two `tc=' fields,
//     xterm's (to xterm-basic, which resolves) and xterm-color's (to xterm-r6, which
//     is not in the file), so nothing in it goes deeper than ONE hop.
//
//   * BUFSIZ IS SPELLED TBUFSIZ.  v7 defined its own BUFSIZ here, which was harmless
//     only because this file includes no <stdio.h>.  Renamed rather than left as a
//     landmine for whoever adds a printf to it.
//
// One v7 bug is fixed rather than carried: tnchktc() copied the `tc=' name into a
// 16-character array with an unbounded strcpy.
//
#include <ctype.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <term.h>
#include <unistd.h>

#define TBUFSIZ   1024 // the caller's bp, and every buffer here
#define MAXHOP    4    // max number of tc= indirections; see above
#define E_TERMCAP "/etc/termcap"

static char *tbuf;
static int hopcount; // detect infinite loops in termcap, init 0

static int tnchktc(void);

//
// Tnamatch deals with name matching.  The first field of the termcap entry is a
// sequence of names separated by |'s, so we compare against each such name.  The
// normal : terminator after the last name (before the first field) stops us.
//
static int tnamatch(char *np)
{
    register char *Np, *Bp;

    Bp = tbuf;
    if (*Bp == '#')
        return (0);
    for (;;) {
        for (Np = np; *Np && *Bp == *Np; Bp++, Np++)
            continue;
        if (*Np == 0 && (*Bp == '|' || *Bp == ':' || *Bp == 0))
            return (1);
        while (*Bp && *Bp != ':' && *Bp != '|')
            Bp++;
        if (*Bp == 0 || *Bp == ':')
            return (0);
        Bp++;
    }
}

//
// tnchktc: check the last entry, see if it's tc=xxx.  If so, recursively find xxx and
// append that entry (minus the names) to take the place of the tc=xxx entry.  This
// allows termcap entries to say "like an HP2621 but doesn't turn on the labels".
// Note that this works because of the left to right scan.
//
static int tnchktc(void)
{
    register char *p, *q;
    char tcname[16]; // name of similar terminal
    char tcbuf[TBUFSIZ];
    char *holdtbuf = tbuf;
    int i, n, l;

    // Walk back from the end for the colon that begins the last field.  v7 ran a
    // pointer down and tested `p < tbuf' to detect running off the front; that is the
    // relational hazard, so the cursor is an INDEX and the test is on the index.
    n = (int)strlen(tbuf) - 2; // before the last colon
    while (--n >= 0 && tbuf[n] != ':')
        continue;
    if (n < 0) {
        write(2, "Bad termcap entry\n", 18);
        return (0);
    }
    p = tbuf + n + 1;
    // p now points to beginning of last field
    if (p[0] != 't' || p[1] != 'c')
        return (1);

    // v7's `strcpy(tcname, p+3)' had no bound: a long tc= name overran tcname and the
    // stack behind it.  Copy up to the field terminator, and no further than the array.
    q = p + 3;
    for (i = 0; i < (int)sizeof(tcname) - 1 && *q && *q != ':'; i++)
        tcname[i] = *q++;
    tcname[i] = 0;

    if (++hopcount > MAXHOP) {
        write(2, "Infinite tc= loop\n", 18);
        return (0);
    }
    if (tgetent(tcbuf, tcname) != 1) {
        hopcount = 0; // unwind recursion
        return (0);
    }
    for (q = tcbuf; *q != ':'; q++)
        ;
    // v7 truncated with `q[BUFSIZ - (p-tbuf)]', and tbuf at this point is the RECURSIVE
    // call's buffer (tcbuf), not the one p points into -- a difference of two unrelated
    // objects.  l was computed from holdtbuf; so is the cut.
    l = (int)(p - holdtbuf) + (int)strlen(q);
    if (l > TBUFSIZ) {
        write(2, "Termcap entry too long\n", 23);
        q[TBUFSIZ - (p - holdtbuf)] = 0;
    }
    strcpy(p, q + 1);
    tbuf     = holdtbuf;
    hopcount = 0; // unwind recursion
    return (1);
}

//
// Get an entry for terminal name in buffer bp, from the termcap file.  Parse is very
// rudimentary; we just notice escaped newlines.
//
int tgetent(char *bp, char *name)
{
    register char *cp;
    register int c;
    register int i = 0, cnt = 0;
    char ibuf[TBUFSIZ];
    int n; // characters in bp -- see the note on `char *' comparisons above
    int tf;

    tbuf = bp;
    tf   = -1;
    cp   = getenv("TERMCAP");
    // TERMCAP can have one of two things in it.  It can be the name of a file to use
    // instead of /etc/termcap.  In this case it better start with a "/".  Or it can be
    // an entry to use so we don't have to read the file.  In this case it has to
    // already have the newlines crunched out.
    if (cp && *cp) {
        if (*cp == '/') {
            tf = open(cp, 0);
        } else {
            tbuf = cp;
            c    = tnamatch(name);
            tbuf = bp;
            if (c) {
                // v7 copied the environment string into the caller's buffer with an
                // unbounded strcpy.  bp holds TBUFSIZ characters and no more.
                strncpy(bp, cp, TBUFSIZ - 1);
                bp[TBUFSIZ - 1] = 0;
                return (tnchktc());
            }
        }
    }
    if (tf < 0)
        tf = open(E_TERMCAP, 0);
    if (tf < 0)
        return (-1);
    for (;;) {
        cp = bp;
        n  = 0;
        for (;;) {
            if (i == cnt) {
                cnt = read(tf, ibuf, TBUFSIZ);
                if (cnt <= 0) {
                    close(tf);
                    return (0);
                }
                i = 0;
            }
            c = ibuf[i++];
            if (c == '\n') {
                // A backslash before the newline continues the entry.  `cp > bp' was
                // the relational hazard; n is the same test on the count.
                if (n > 0 && cp[-1] == '\\') {
                    cp--;
                    n--;
                    continue;
                }
                break;
            }
            // TBUFSIZ - 1, not TBUFSIZ: `*cp = 0' below still has to fit.  v7's
            // `cp >= bp+BUFSIZ' let the terminator land one past the end.
            if (n >= TBUFSIZ - 1) {
                write(2, "Termcap entry too long\n", 23);
                break;
            } else {
                *cp++ = c;
                n++;
            }
        }
        *cp = 0;

        // The real work for the match.
        if (tnamatch(name)) {
            close(tf);
            return (tnchktc());
        }
    }
}

//
// Skip to the next field.  Notice that this is very dumb, not knowing about \: escapes
// or any such.  If necessary, :'s can be put into the termcap file in octal.
//
static char *tskip(char *bp)
{
    while (*bp && *bp != ':')
        bp++;
    if (*bp == ':')
        bp++;
    return (bp);
}

//
// Return the (numeric) option id.  Numeric options look like
//      li#80
// i.e. the option string is separated from the numeric value by a # character.  If the
// option is not found we return -1.  Note that we handle octal numbers beginning with 0.
//
int tgetnum(char *id)
{
    register int i, base;
    register char *bp = tbuf;

    for (;;) {
        bp = tskip(bp);
        if (*bp == 0)
            return (-1);
        if (*bp++ != id[0] || *bp == 0 || *bp++ != id[1])
            continue;
        if (*bp == '@')
            return (-1);
        if (*bp != '#')
            continue;
        bp++;
        base = 10;
        if (*bp == '0')
            base = 8;
        i = 0;
        while (isdigit(*bp))
            i *= base, i += *bp++ - '0';
        return (i);
    }
}

//
// Handle a flag option.  Flag options are given "naked", i.e. followed by a : or the end
// of the buffer.  Return 1 if we find the option, or 0 if it is not given.
//
int tgetflag(char *id)
{
    register char *bp = tbuf;

    for (;;) {
        bp = tskip(bp);
        if (!*bp)
            return (0);
        if (*bp++ == id[0] && *bp != 0 && *bp++ == id[1]) {
            if (!*bp || *bp == ':')
                return (1);
            else if (*bp == '@')
                return (0);
        }
    }
}

//
// Tdecode does the grung work to decode the string capability escapes.
//
static char *tdecode(char *str, char **area)
{
    register char *cp;
    register int c;
    register char *dp;
    int i;

    cp = *area;
    while ((c = *str++) && c != ':') {
        switch (c) {
        case '^':
            c = *str++ & 037;
            break;

        case '\\':
            dp = "E\033^^\\\\::n\nr\rt\tb\bf\f";
            c  = *str++;
        nextc:
            if (*dp++ == c) {
                c = *dp++;
                break;
            }
            dp++;
            if (*dp)
                goto nextc;
            if (isdigit(c)) {
                c -= '0', i = 2;
                do
                    c <<= 3, c |= *str++ - '0';
                while (--i && isdigit(*str));
            }
            break;
        }
        *cp++ = c;
    }
    *cp++ = 0;
    str   = *area;
    *area = cp;
    return (str);
}

//
// Get a string valued option.  These are given as
//      cl=^Z
// Much decoding is done on the strings, and the strings are placed in area, which is a
// ref parameter which is updated.  No checking on area overflow.
//
char *tgetstr(char *id, char **area)
{
    register char *bp = tbuf;

    for (;;) {
        bp = tskip(bp);
        if (!*bp)
            return (0);
        if (*bp++ != id[0] || *bp == 0 || *bp++ != id[1])
            continue;
        if (*bp == '@')
            return (0);
        if (*bp != '=')
            continue;
        bp++;
        return (tdecode(bp, area));
    }
}
