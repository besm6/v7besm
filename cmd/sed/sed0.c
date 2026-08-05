/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// sed -- the compiler: a script in, a ptrspace[] of compiled commands out.
//
// Task C5e.  See README.md beside this file; what follows is only what a reader of the
// diff needs, and sed.h carries the constants' own reasons.
//
// THE CHARACTER CLASS IS 256 BITS and the `y' TABLE IS 256 ENTRIES, where v7's were 128
// of each -- see CCLSIZE and YSIZE in sed.h.  Both were indexed by an unmasked byte on
// one side, so on a machine whose text is UTF-8 and whose char is unsigned the first was
// a wild STORE into the arena and the second a wild READ out of it.
//
// A REPLACEMENT TEXT MARKS AN ESCAPE WITH A PREFIX BYTE, not with bit 0200 of the byte
// itself -- see QESC in sed.h.  compsub() below is the producer and sed1.c's dosub() the
// only consumer.
//
// FOUR WRITES THAT HAD NO BOUND now have one: text() into a wfile name (a 40-byte row of
// fname[][], which a long name walked straight out of), text() and compsub() into the
// arena (v7 checked `p > reend' AFTER the write), and rline() into linebuf.  compile()
// grew a second bound as well: v7 limited an expression to ESIZE from its own start and
// never asked whether ESIZE bytes were left in the arena.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sed.h"

// A line-number address is stored in the compiled expression as ONE BYTE index into
// tlno[], so the table cannot outgrow what a byte can name.
_Static_assert(NLINES <= 256, "a CLNUM index is stored in one byte of the expression");

static void fcomp(void);
static char *compsub(char *rhsbuf, char *endbuf);
static char *compile(char *expbuf);
static int rline(char *lbuf);
static char *address(char *expbuf);
static char *text(char *textbuf, char *endbuf);
static struct label *search(struct label *ptr);
static void dechain(void);
static char *ycomp(char *expbuf);

static void usage(void)
{
    fprintf(stderr, "usage: sed [-n] [-g] [-e script] [-f sfile] [file ...]\n");
    exit(2);
}

// The arena is full.  v7 reached this by testing the cursor after the write that had
// already passed the end.
static void toomuch(void)
{
    fprintf(stderr, TMMES, linebuf);
    exit(2);
}

// One expression is longer than ESIZE, or than what is left of the arena.  v7 returned
// badp here and the caller said `command garbled', which named the wrong fault; the `['
// case already had this message and only this message.
static void relong(void)
{
    fprintf(stderr, "RE too long: %s\n", linebuf);
    exit(2);
}

static void scriptlong(void)
{
    fprintf(stderr, "sed: script line too long\n");
    exit(2);
}

int main(int argc, char **argv)
{
    eargc = argc;
    eargv = argv;

    badp     = &bad;
    aptr     = abuf;
    lab      = labtab + 1; /* 0 reserved for end-pointer */
    rep      = ptrspace;
    rep->ad1 = respace;
    lbend    = &linebuf[LBSIZE];
    hend     = &holdsp[LBSIZE];
    lcomend  = &genbuf[71];
    ptrend   = &ptrspace[PTRSIZE];
    reend    = &respace[RESIZE];
    labend   = &labtab[LABSIZE];
    lnum     = 0;
    pending  = 0;
    depth    = 0;
    spend    = linebuf;
    hspend   = holdsp;
    fcode[0] = stdout;
    nfiles   = 1;

    // v7 exited 0 here, so `sed' with nothing to do was indistinguishable from `sed'
    // that had done it.
    if (eargc == 1)
        usage();

    while (--eargc > 0 && (++eargv)[0][0] == '-')
        switch (eargv[0][1]) {
        case 'n':
            nflag++;
            continue;

        case 'f':
            if (eargc-- <= 0)
                usage();

            if ((fin = fopen(*++eargv, "r")) == NULL) {
                fprintf(stderr, "Cannot open pattern-file: %s\n", *eargv);
                exit(2);
            }

            fcomp();
            fclose(fin);
            continue;

        case 'e':
            eflag++;
            fcomp();
            eflag = 0;
            continue;

        // -g is v7's and sed.1.umm never mentioned it; the page says so now rather than the
        // flag being deleted, since a script may rely on it.
        case 'g':
            gflag++;
            continue;

        // v7 wrote this diagnostic to STDOUT -- into the edited stream, in the middle of
        // the output the user was collecting -- and then carried on with the flag
        // consumed and ignored.
        case '\0':
            fprintf(stderr, "sed: `-' names no file here\n");
            usage();
            /* NOTREACHED */

        default:
            fprintf(stderr, "sed: unknown flag: %c\n", eargv[0][1]);
            usage();
            /* NOTREACHED */
        }

    if (compfl == 0) {
        eargv--;
        eargc++;
        eflag++;
        fcomp();
        eargv++;
        eargc--;
        eflag = 0;
    }

    if (depth) {
        fprintf(stderr, "Too many {'s\n");
        exit(2);
    }

    labtab->address = rep;

    dechain();

    if (eargc <= 0)
        execute((char *)NULL);
    else
        while (--eargc >= 0)
            execute(*eargv++);

    fclose(stdout);
    return errflag ? 2 : 0;
}

static void fcomp(void)
{
    char *p, *op, *tp;
    struct reptr *pt, *pt1;
    int i;
    struct label *lpt;

    compfl = 1;
    op     = lastre;

    if (rline(linebuf) < 0)
        return;
    if (*linebuf == '#') {
        if (linebuf[1] == 'n')
            nflag = 1;
    } else {
        cp = linebuf;
        goto comploop;
    }

    for (;;) {
        if (rline(linebuf) < 0)
            break;

        cp = linebuf;

    comploop:
        while (*cp == ' ' || *cp == '\t')
            cp++;
        if (*cp == '\0' || *cp == '#')
            continue;
        if (*cp == ';') {
            cp++;
            goto comploop;
        }

        p = address(rep->ad1);
        if (p == badp) {
            fprintf(stderr, CGMES, linebuf);
            exit(2);
        }

        if (p == rep->ad1) {
            if (op)
                rep->ad1 = op;
            else {
                fprintf(stderr, "First RE may not be null\n");
                exit(2);
            }
        } else if (p == 0) {
            p        = rep->ad1;
            rep->ad1 = 0;
        } else {
            op = rep->ad1;
            if (*cp == ',' || *cp == ';') {
                cp++;
                if ((rep->ad2 = p) > reend)
                    toomuch();
                p = address(rep->ad2);
                if (p == badp || p == 0) {
                    fprintf(stderr, CGMES, linebuf);
                    exit(2);
                }
                if (p == rep->ad2)
                    rep->ad2 = op;
                else
                    op = rep->ad2;

            } else
                rep->ad2 = 0;
        }

        if (p > reend)
            toomuch();

        while (*cp == ' ' || *cp == '\t')
            cp++;

    swit:
        switch (*cp++) {
        default:
            fprintf(stderr, "Unrecognized command: %s\n", linebuf);
            exit(2);

        case '!':
            rep->negfl = 1;
            goto swit;

        case '{':
            rep->command = BCOM;
            rep->negfl   = !(rep->negfl);
            if (depth >= DEPTH) {
                fprintf(stderr, "Too many {'s\n");
                exit(2);
            }
            cmpend[depth++] = &rep->u.lb1;
            if (++rep >= ptrend) {
                fprintf(stderr, "Too many commands: %s\n", linebuf);
                exit(2);
            }
            rep->ad1 = p;
            if (*cp == '\0')
                continue;

            goto comploop;

        case '}':
            if (rep->ad1) {
                fprintf(stderr, AD0MES, linebuf);
                exit(2);
            }

            if (--depth < 0) {
                fprintf(stderr, "Too many }'s\n");
                exit(2);
            }
            *cmpend[depth] = rep;

            rep->ad1 = p;
            continue;

        case '=':
            rep->command = EQCOM;
            if (rep->ad2) {
                fprintf(stderr, AD1MES, linebuf);
                exit(2);
            }
            break;

        case ':':
            if (rep->ad1) {
                fprintf(stderr, AD0MES, linebuf);
                exit(2);
            }

            while (*cp++ == ' ')
                ;
            cp--;

            // TWO BOUNDS, and v7 had both of them one place too tight against its own
            // arrays.  asc is LABSZ + 1 bytes -- eight characters and a NUL -- and v7
            // refused the eighth; and the label table is checked here, BEFORE the name is
            // copied into the slot, where v7 copied first and tested the incremented
            // cursor, which refused the last slot it had just written.
            if (lab >= labend) {
                fprintf(stderr, "Too many labels: %s\n", linebuf);
                exit(2);
            }
            tp = lab->asc;
            while ((*tp++ = *cp++))
                if (tp > &lab->asc[LABSZ]) {
                    fprintf(stderr, LTL, linebuf);
                    exit(2);
                }
            *--tp = '\0';

            if ((lpt = search(lab))) {
                if (lpt->address) {
                    fprintf(stderr, "Duplicate labels: %s\n", linebuf);
                    exit(2);
                }
            } else {
                lab->chain = 0;
                lpt        = lab;
                lab++;
            }
            lpt->address = rep;
            rep->ad1     = p;

            continue;

        case 'a':
            rep->command = ACOM;
            if (rep->ad2) {
                fprintf(stderr, AD1MES, linebuf);
                exit(2);
            }
            if (*cp == '\\')
                cp++;
            if (*cp++ != '\n') {
                fprintf(stderr, CGMES, linebuf);
                exit(2);
            }
            rep->u.re1 = p;
            p          = text(rep->u.re1, reend);
            if (p == badp)
                toomuch();
            break;

        case 'c':
            rep->command = CCOM;
            if (*cp == '\\')
                cp++;
            if (*cp++ != '\n') {
                fprintf(stderr, CGMES, linebuf);
                exit(2);
            }
            rep->u.re1 = p;
            p          = text(rep->u.re1, reend);
            if (p == badp)
                toomuch();
            break;

        case 'i':
            rep->command = ICOM;
            if (rep->ad2) {
                fprintf(stderr, AD1MES, linebuf);
                exit(2);
            }
            if (*cp == '\\')
                cp++;
            if (*cp++ != '\n') {
                fprintf(stderr, CGMES, linebuf);
                exit(2);
            }
            rep->u.re1 = p;
            p          = text(rep->u.re1, reend);
            if (p == badp)
                toomuch();
            break;

        case 'g':
            rep->command = GCOM;
            break;

        case 'G':
            rep->command = CGCOM;
            break;

        case 'h':
            rep->command = HCOM;
            break;

        case 'H':
            rep->command = CHCOM;
            break;

        case 't':
            rep->command = TCOM;
            goto jtcommon;

        case 'b':
            rep->command = BCOM;
        jtcommon:
            while (*cp++ == ' ')
                ;
            cp--;

            if (*cp == '\0') {
                if ((pt = labtab->chain)) {
                    while ((pt1 = pt->u.lb1))
                        pt = pt1;
                    pt->u.lb1 = rep;
                } else
                    labtab->chain = rep;
                break;
            }
            if (lab >= labend) {
                fprintf(stderr, "Too many labels: %s\n", linebuf);
                exit(2);
            }
            tp = lab->asc;
            while ((*tp++ = *cp++))
                if (tp > &lab->asc[LABSZ]) {
                    fprintf(stderr, LTL, linebuf);
                    exit(2);
                }
            cp--;
            *--tp = '\0';

            if ((lpt = search(lab))) {
                if (lpt->address) {
                    rep->u.lb1 = lpt->address;
                } else {
                    pt = lpt->chain;
                    while ((pt1 = pt->u.lb1))
                        pt = pt1;
                    pt->u.lb1 = rep;
                }
            } else {
                lab->chain   = rep;
                lab->address = 0;
                lab++;
            }
            break;

        case 'n':
            rep->command = NCOM;
            break;

        case 'N':
            rep->command = CNCOM;
            break;

        case 'p':
            rep->command = PCOM;
            break;

        case 'P':
            rep->command = CPCOM;
            break;

        case 'r':
            rep->command = RCOM;
            if (rep->ad2) {
                fprintf(stderr, AD1MES, linebuf);
                exit(2);
            }
            if (*cp++ != ' ') {
                fprintf(stderr, CGMES, linebuf);
                exit(2);
            }
            rep->u.re1 = p;
            p          = text(rep->u.re1, reend);
            if (p == badp)
                toomuch();
            break;

        case 'd':
            rep->command = DCOM;
            break;

        case 'D':
            rep->command = CDCOM;
            rep->u.lb1   = ptrspace;
            break;

        case 'q':
            rep->command = QCOM;
            if (rep->ad2) {
                fprintf(stderr, AD1MES, linebuf);
                exit(2);
            }
            break;

        case 'l':
            rep->command = LCOM;
            break;

        case 's':
            rep->command = SCOM;
            seof         = *cp++;
            rep->u.re1   = p;
            p            = compile(rep->u.re1);
            if (p == badp) {
                fprintf(stderr, CGMES, linebuf);
                exit(2);
            }
            if (p == rep->u.re1)
                rep->u.re1 = op;
            else
                op = rep->u.re1;

            if ((rep->rhs = p) > reend)
                toomuch();

            if ((p = compsub(rep->rhs, reend)) == badp) {
                fprintf(stderr, CGMES, linebuf);
                exit(2);
            }
            if (*cp == 'g') {
                cp++;
                rep->gfl++;
            } else if (gflag)
                rep->gfl++;

            if (*cp == 'p') {
                cp++;
                rep->pfl = 1;
            }

            // s///P is v7's and sed.1.umm never mentioned it either: print only as far as the
            // first newline, the way the P command does.
            if (*cp == 'P') {
                cp++;
                rep->pfl = 2;
            }

            if (*cp == 'w') {
                cp++;
                if (*cp++ != ' ') {
                    fprintf(stderr, CGMES, linebuf);
                    exit(2);
                }
                if (nfiles >= WFILES) {
                    fprintf(stderr, "Too many files in w commands\n");
                    exit(2);
                }

                if (text(fname[nfiles], &fname[nfiles][FNSIZE]) == badp) {
                    fprintf(stderr, "File name too long: %s\n", linebuf);
                    exit(2);
                }
                for (i = nfiles - 1; i >= 0; i--)
                    if (strcmp(fname[nfiles], fname[i]) == 0) {
                        rep->fcode = fcode[i];
                        goto done;
                    }
                if ((rep->fcode = fopen(fname[nfiles], "w")) == NULL) {
                    fprintf(stderr, "cannot open %s\n", fname[nfiles]);
                    exit(2);
                }
                fcode[nfiles++] = rep->fcode;
            }
            break;

        case 'w':
            rep->command = WCOM;
            if (*cp++ != ' ') {
                fprintf(stderr, CGMES, linebuf);
                exit(2);
            }
            if (nfiles >= WFILES) {
                fprintf(stderr, "Too many files in w commands\n");
                exit(2);
            }

            if (text(fname[nfiles], &fname[nfiles][FNSIZE]) == badp) {
                fprintf(stderr, "File name too long: %s\n", linebuf);
                exit(2);
            }
            for (i = nfiles - 1; i >= 0; i--)
                if (strcmp(fname[nfiles], fname[i]) == 0) {
                    rep->fcode = fcode[i];
                    goto done;
                }

            if ((rep->fcode = fopen(fname[nfiles], "w")) == NULL) {
                fprintf(stderr, "Cannot create %s\n", fname[nfiles]);
                exit(2);
            }
            fcode[nfiles++] = rep->fcode;
            break;

        case 'x':
            rep->command = XCOM;
            break;

        case 'y':
            rep->command = YCOM;
            seof         = *cp++;
            rep->u.re1   = p;
            p            = ycomp(rep->u.re1);
            if (p == badp) {
                fprintf(stderr, CGMES, linebuf);
                exit(2);
            }
            break;
        }
    done:
        if (++rep >= ptrend) {
            fprintf(stderr, "Too many commands, last: %s\n", linebuf);
            exit(2);
        }

        rep->ad1 = p;

        if (*cp++ != '\0') {
            if (cp[-1] == ';')
                goto comploop;
            fprintf(stderr, CGMES, linebuf);
            exit(2);
        }
    }
    rep->command = 0;
    lastre       = op;
}

// The replacement text of an `s' command, marking the bytes that arrived
// backslash-escaped.  See QESC in sed.h: the mark is a prefix byte and no longer bit 0200
// of the byte itself, so a Cyrillic letter in a replacement survives.
//
// The `esc' flag carries v7's control flow across the change.  There, an escaped byte
// could not compare equal to the delimiter because the 0200 bit had already made it
// unequal -- which is how `s/x/\//' inserts the delimiter.  Testing `!esc' asks the same
// question.
static char *compsub(char *rhsbuf, char *endbuf)
{
    char *p, *q;
    int c, esc;

    p = rhsbuf;
    q = cp;
    for (;;) {
        esc = 0;
        c   = *q++;
        if (c == '\\') {
            c   = *q++;
            esc = 1;
            if (c > numbra + '0' && c <= '9')
                return badp;
        }
        // An unterminated replacement, escaped or not.  v7 tested only the unescaped
        // case, so a script ending in a backslash walked q off the end of linebuf.
        if (c == '\0')
            return badp;
        if (!esc && c == seof) {
            if (p >= endbuf)
                toomuch();
            *p++ = '\0';
            cp   = q;
            return p;
        }
        if (esc || c == QESC) {
            if (p >= endbuf)
                toomuch();
            *p++ = QESC;
        }
        if (p >= endbuf)
            toomuch();
        *p++ = c;
    }
}

static char *compile(char *expbuf)
{
    int c;
    char *ep, *sp;
    int neg;
    char *lastep, *cstart;
    int cclcnt;
    int closed;
    char bracket[NBRA], *bracketp;
    char *eend;

    if (*cp == seof) {
        cp++;
        return expbuf;
    }

    ep = expbuf;
    // TWO bounds, where v7 had one.  ESIZE limits an expression from its own start; reend
    // limits the arena every expression is carved out of, and an expression compiled near
    // the end of the arena reaches that one first.  v7 tested only ESIZE and would write
    // past respace.
    eend = expbuf + ESIZE;
    if (eend > reend)
        eend = reend;

    lastep   = 0;
    bracketp = bracket;
    closed = numbra = 0;
    sp              = cp;
    if (*sp == '^') {
        *ep++ = 1;
        sp++;
    } else {
        *ep++ = 0;
    }
    for (;;) {
        if (ep >= eend)
            relong();
        if ((c = *sp++) == seof) {
            if (bracketp != bracket) {
                cp = sp;
                return badp;
            }
            cp    = sp;
            *ep++ = CEOF;
            return ep;
        }
        if (c != '*')
            lastep = ep;
        switch (c) {
        case '\\':
            if ((c = *sp++) == '(') {
                if (numbra >= NBRA) {
                    cp = sp;
                    return badp;
                }
                *bracketp++ = numbra;
                *ep++       = CBRA;
                *ep++       = numbra++;
                continue;
            }
            if (c == ')') {
                if (bracketp <= bracket) {
                    cp = sp;
                    return badp;
                }
                *ep++ = CKET;
                *ep++ = *--bracketp;
                closed++;
                continue;
            }

            if (c >= '1' && c <= '9') {
                if ((c -= '1') >= closed)
                    return badp;

                *ep++ = CBACK;
                *ep++ = c;
                continue;
            }
            if (c == '\n') {
                cp = sp;
                return badp;
            }
            if (c == 'n')
                c = '\n';
            goto defchar;

        // v7 treated a NUL inside an expression as a no-op and went round again -- but a
        // NUL is the end of the script line, so an unterminated `s/a' or `/a' walked sp
        // off the end of linebuf and compiled whatever it found there until the ESIZE
        // bound stopped it.  There is no legitimate NUL in an expression; it is the same
        // fault as an embedded newline and gets the same answer.
        case '\0':
        case '\n':
            cp = sp;
            return badp;

        case '.':
            *ep++ = CDOT;
            continue;

        case '*':
            if (lastep == 0)
                goto defchar;
            if (*lastep == CKET) {
                cp = sp;
                return badp;
            }
            *lastep |= STAR;
            continue;

        case '$':
            if (*sp != seof)
                goto defchar;
            *ep++ = CDOL;
            continue;

        case '[':
            if (&ep[CCLSIZE + 1] >= eend)
                relong();

            *ep++ = CCL;
            // The arena is reused across a whole script, so the class has to be zeroed
            // rather than assumed zero.  grep's expbuf is one bss array and could assume.
            memset(ep, 0, CCLSIZE);

            neg = 0;
            if ((c = *sp++) == '^') {
                neg = 1;
                c   = *sp++;
            }

            cstart = sp;
            do {
                if (c == '\0') {
                    fprintf(stderr, CGMES, linebuf);
                    exit(2);
                }
                if (c == '-' && sp > cstart && *sp != ']') {
                    // A range is a range of BYTES, as ed(1)'s and grep(1)'s classes are,
                    // so a range written between two multi-byte letters is not what a
                    // reader expects.  sed.1.umm says so.
                    for (c = sp[-2]; c < *sp; c++)
                        ep[c >> 3] |= bittab[c & 07];
                }
                if (c == '\\') {
                    switch (c = *sp++) {
                    case 'n':
                        c = '\n';
                        break;
                    }
                }

                // No mask on c: CCLSIZE bytes hold every value a byte can take, so
                // c >> 3 is in 0..31 by construction.  v7 masked NEITHER side here and
                // stored past the class for any byte above 0177.
                ep[c >> 3] |= bittab[c & 07];
            } while ((c = *sp++) != ']');

            if (neg)
                for (cclcnt = 0; cclcnt < CCLSIZE; cclcnt++)
                    ep[cclcnt] ^= 0377; // v7 wrote ^= -1, into a char
            // A class never matches the terminator.  v7 put this outside the negation,
            // where it is a no-op for a positive class -- a NUL cannot get into one, the
            // `c == '\0'' arm above having already exited.  Left where v7 had it.
            ep[0] &= 0376;

            ep += CCLSIZE;

            continue;

        defchar:
        default:
            *ep++ = CCHR;
            *ep++ = c;
        }
    }
}

// One line of the script, from -e (which may carry several, separated by newlines) or
// from an -f file.  A trailing backslash hides the newline.
static int rline(char *lbuf)
{
    char *q;
    int t, n;
    static char *saveq;

    n = 0;
    if (eflag) {
        if (eflag > 0) {
            eflag = -1;
            if (eargc-- <= 0)
                usage();
            q = *++eargv;
        } else if ((q = saveq) == 0)
            return -1;

        // v7 wrote these two arms out twice and they were NOT identical: the continuation
        // copy compared a backslash-escaped byte with '0' where the first compared it
        // with '\0', so a `\0' anywhere but on the first line of a -e script silently
        // ended the script.  One loop cannot disagree with itself.
        while ((lbuf[n] = *q++) != '\0') {
            if (lbuf[n] == '\\') {
                if ((lbuf[n + 1] = *q++) == '\0') {
                    saveq = 0;
                    return -1;
                }
                n++;
            } else if (lbuf[n] == '\n') {
                lbuf[n] = '\0';
                saveq   = q;
                return 1;
            }
            if (++n >= LBSIZE)
                scriptlong();
        }
        saveq = 0;
        return 1;
    }

    while ((t = getc(fin)) != EOF) {
        lbuf[n] = t;
        if (lbuf[n] == '\\') {
            // v7 stored the EOF itself here, which on this machine is the byte 0377 --
            // and 0377 is now QESC.
            if ((t = getc(fin)) == EOF)
                break;
            lbuf[n + 1] = t;
            n++;
        } else if (lbuf[n] == '\n') {
            lbuf[n] = '\0';
            return 1;
        }
        if (++n >= LBSIZE)
            scriptlong();
    }
    lbuf[n] = '\0';
    return -1;
}

static char *address(char *expbuf)
{
    char *rcp;
    int lno;

    if (*cp == '$') {
        cp++;
        *expbuf++ = CEND;
        *expbuf++ = CEOF;
        return expbuf;
    }

    if (*cp == '/') {
        seof = '/';
        cp++;
        return compile(expbuf);
    }

    rcp = cp;
    lno = 0;

    while (*rcp >= '0' && *rcp <= '9')
        lno = lno * 10 + *rcp++ - '0';

    if (rcp > cp) {
        // v7 tested this AFTER the store, so the table held NLINES entries and refused
        // the NLINES'th.  Bracketing it here makes the stated limit the real one.
        if (nlno >= NLINES) {
            fprintf(stderr, "Too many line numbers\n");
            exit(2);
        }
        *expbuf++    = CLNUM;
        *expbuf++    = nlno;
        tlno[nlno++] = lno;
        *expbuf++    = CEOF;
        cp           = rcp;
        return expbuf;
    }
    return 0;
}

// The text of an a, c, i or r command, or the name of a w file.  v7 took no end and had
// none: `text(fname[nfiles])' wrote into a forty-byte row of fname[][] with nothing
// between a long name and fcode[] behind it, and the arena callers tested the cursor
// against reend only after the write that had already passed it.
static char *text(char *textbuf, char *endbuf)
{
    char *p, *q;

    p = textbuf;
    q = cp;
    while (*q == '\t' || *q == ' ')
        q++;
    for (;;) {
        if (p >= endbuf)
            return badp;
        if ((*p = *q++) == '\\')
            *p = *q++;
        if (*p == '\0') {
            cp = --q;
            return ++p;
        }
        if (*p == '\n')
            while (*q == '\t' || *q == ' ')
                q++;
        p++;
    }
}

static struct label *search(struct label *ptr)
{
    struct label *rp;

    rp = labtab;
    while (rp < ptr) {
        if (strcmp(rp->asc, ptr->asc) == 0)
            return rp;
        rp++;
    }

    return 0;
}

static void dechain(void)
{
    struct label *lptr;
    struct reptr *rptr, *trptr;

    for (lptr = labtab; lptr < lab; lptr++) {
        if (lptr->address == 0) {
            fprintf(stderr, "Undefined label: %s\n", lptr->asc);
            exit(2);
        }

        if (lptr->chain) {
            rptr = lptr->chain;
            while ((trptr = rptr->u.lb1)) {
                rptr->u.lb1 = lptr->address;
                rptr        = trptr;
            }
            rptr->u.lb1 = lptr->address;
        }
    }
}

// The y/// translation table: YSIZE entries, one per byte value, where v7's was 128.
// v7's width was written as a loop condition (`!(c & 0200)') and a pointer bump
// (`ep + 0200') and as a number nowhere at all, so grepping cmd/sed for it found neither;
// and the source characters were masked with 0177 on the way in, which folded every byte
// of a Cyrillic letter onto an ASCII slot.  sed1.c's YCOM indexes this table with an
// unmasked byte from the pattern space, so at 128 entries `y' read past the end of it for
// any byte above 0177 -- into whatever compile() had put next in the arena -- and wrote
// what it found back into the line.
static char *ycomp(char *expbuf)
{
    char *ep, *tsp;
    char *sp;
    int c;

    ep = expbuf;
    if (ep + YSIZE > reend)
        toomuch();
    sp = cp;
    for (tsp = cp; *tsp != seof; tsp++) {
        if (*tsp == '\\')
            tsp++;
        // v7 walked off the end of linebuf for an unterminated y command: it looked for
        // the delimiter and for a newline, and rline() leaves neither behind.
        if (*tsp == '\0' || *tsp == '\n')
            return badp;
    }
    tsp++;

    memset(ep, 0, YSIZE);
    while ((c = *sp++) != seof) {
        if (c == '\\' && *sp == 'n') {
            sp++;
            c = '\n';
        }
        if ((ep[c] = *tsp++) == '\\' && *tsp == 'n') {
            ep[c] = '\n';
            tsp++;
        }
        if (ep[c] == seof || ep[c] == '\0')
            return badp;
    }
    if (*tsp != seof)
        return badp;
    cp = ++tsp;

    for (c = 0; c < YSIZE; c++)
        if (ep[c] == 0)
            ep[c] = c;

    return ep + YSIZE;
}
