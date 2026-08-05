/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// sed -- the executor: the compiled ptrspace[] applied to each input line.
//
// Task C5e.  See README.md beside this file; sed.h carries the constants' reasons.
//
// FOUR THINGS A READER OF THE DIFF NEEDS.
//
// dosub() DECODES A PREFIX BYTE, not bit 0200.  v7 tested `c & 0200' to tell `\1' from
// `1' and then wrote `*sp++ = c & 0177' for every OTHER byte -- so on a machine whose
// text is UTF-8 every Cyrillic byte of a replacement lost its eighth bit and
// `s/x/привет/' produced ten bytes of plausible ASCII.  See QESC in sed.h.
//
// THE THREE genbuf BOUNDS NOW STOP.  v7 wrote `if (sp >= &genbuf[LBSIZE]) fprintf(...)'
// three times, with no exit, no break and no return behind any of them: the bound was
// announced and the loop went on writing past the buffer.  They are int indices that
// diagnose and exit, the way ed(1)'s place() does -- and the input side agrees with them,
// where v7 truncated a long input line in silence.  sort(1) settled that in task C5d: one
// line limit, loud, and the same on every path.
//
// advance() COUNTS ITS RECURSION.  See MAXDEPTH in sed.h.
//
// THE l COMMAND SPELLS AN ESCAPE IN THREE OCTAL DIGITS.  sed.1.umm promised two and an
// "unambiguous" listing, and the two could not both be true: v7's table printed a RAW
// newline for 012 and a RAW DEL for 0177, so `l' -- whose entire job is making a line
// legible -- rendered two of the characters that most need it as themselves.  A byte
// above 0177 is passed through rather than escaped, which is ed(1)'s answer for the same
// command and is what a console speaking UTF-8 wants.
//

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sed.h"

// The escapes the l command prints for 001..037.  Index i is the character i+1.  v7 wrote
// two octal digits, a raw newline at index 9 and `<-'/`>-' for backspace and tab; the
// overstrike pair is kept -- ed(1) prints the same two characters -- and the octal is
// three digits so that it can also spell 0177.
static char *trans[037] = { "\\001", "\\002", "\\003", "\\004", "\\005", "\\006", "\\007", "<-",
                            ">-",    "\\012", "\\013", "\\014", "\\015", "\\016", "\\017", "\\020",
                            "\\021", "\\022", "\\023", "\\024", "\\025", "\\026", "\\027", "\\030",
                            "\\031", "\\032", "\\033", "\\034", "\\035", "\\036", "\\037" };
static char rub[]       = "\\177";

static int match(char *expbuf, int gf);
static int advance(char *alp, char *aep);
static int advance1(char *alp, char *aep);
static int substitute(struct reptr *ipc);
static void dosub(char *rhsbuf);
static int place(int sn, char *l1, char *l2);
static void command(struct reptr *ipc);
static char *gline(char *addr);
static int ecmp(char *a, char *b, int count);
static void arout(void);

static void toolong(void)
{
    fprintf(stderr, "sed: output line too long\n");
    exit(2);
}

void execute(char *file)
{
    char *p1, *p2;
    struct reptr *ipc;
    int c;
    char *execp;

    if (file) {
        // v7 diagnosed and then carried on with a descriptor of -1, so the read failed,
        // the file was skipped and sed exited 0 -- a script could not tell a missing
        // input from an empty one.
        if ((infile = open(file, 0)) < 0) {
            fprintf(stderr, "Can't open %s\n", file);
            errflag = 1;
            return;
        }
    } else
        infile = 0;

    ebp = ibuf;
    cbp = ibuf;

    if (pending) {
        ipc     = pending;
        pending = 0;
        goto yes;
    }

    for (;;) {
        if ((execp = gline(linebuf)) == badp) {
            close(infile);
            return;
        }
        spend = execp;

        for (ipc = ptrspace; ipc->command;) {
            p1 = ipc->ad1;
            p2 = ipc->ad2;

            if (p1) {
                if (ipc->inar) {
                    if (*p2 == CEND) {
                        p1 = 0;
                    } else if (*p2 == CLNUM) {
                        c = *(unsigned char *)&p2[1];
                        if (lnum > tlno[c]) {
                            ipc->inar = 0;
                            if (ipc->negfl)
                                goto yes;
                            ipc++;
                            continue;
                        }
                        if (lnum == tlno[c])
                            ipc->inar = 0;
                    } else if (match(p2, 0)) {
                        ipc->inar = 0;
                    }
                } else if (*p1 == CEND) {
                    if (!dolflag) {
                        if (ipc->negfl)
                            goto yes;
                        ipc++;
                        continue;
                    }

                } else if (*p1 == CLNUM) {
                    c = *(unsigned char *)&p1[1];
                    if (lnum != tlno[c]) {
                        if (ipc->negfl)
                            goto yes;
                        ipc++;
                        continue;
                    }
                    if (p2)
                        ipc->inar = 1;
                } else if (match(p1, 0)) {
                    if (p2)
                        ipc->inar = 1;
                } else {
                    if (ipc->negfl)
                        goto yes;
                    ipc++;
                    continue;
                }
            }

            if (ipc->negfl) {
                ipc++;
                continue;
            }
        yes:
            command(ipc);

            if (delflag)
                break;

            if (jflag) {
                jflag = 0;
                if ((ipc = ipc->u.lb1) == 0) {
                    ipc = ptrspace;
                    break;
                }
            } else
                ipc++;
        }
        if (!nflag && !delflag) {
            for (p1 = linebuf; p1 < spend; p1++)
                putc(*p1, stdout);
            putc('\n', stdout);
        }

        if (aptr > abuf)
            arout();

        delflag = 0;
    }
}

static int match(char *expbuf, int gf)
{
    char *p1, *p2;
    int c;

    if (gf) {
        if (*expbuf)
            return 0;
        p1 = linebuf;
        p2 = genbuf;
        while ((*p1++ = *p2++))
            ;
        locs = p1 = loc2;
    } else {
        p1   = linebuf;
        locs = 0;
    }

    p2      = expbuf;
    redepth = 0;
    if (*p2++) {
        loc1 = p1;
        if (*p2 == CCHR && p2[1] != *p1)
            return 0;
        return advance(p1, p2);
    }

    /* fast check for first character */

    if (*p2 == CCHR) {
        c = p2[1];
        do {
            if (*p1 != c)
                continue;
            if (advance(p1, p2)) {
                loc1 = p1;
                return 1;
            }
        } while (*p1++);
        return 0;
    }

    do {
        if (advance(p1, p2)) {
            loc1 = p1;
            return 1;
        }
    } while (*p1++);
    return 0;
}

// The counted wrapper.  advance1() is v7's advance() unchanged but for the four places it
// calls itself, which come back through here so that every level is counted.  See
// MAXDEPTH in sed.h: past the ceiling the matcher returns a wrong answer long before it
// faults, which is what this exists to prevent.
static int advance(char *alp, char *aep)
{
    int r;

    if (++redepth > MAXDEPTH) {
        fprintf(stderr, "sed: expression too complex\n");
        exit(2);
    }
    r = advance1(alp, aep);
    redepth--;
    return r;
}

static int advance1(char *alp, char *aep)
{
    char *lp, *ep, *curlp;
    int c;
    char *bbeg;
    int ct;

    lp = alp;
    ep = aep;
    for (;;)
        switch (*ep++) {
        case CCHR:
            if (*ep++ == *lp++)
                continue;
            return 0;

        case CDOT:
            if (*lp++)
                continue;
            return 0;

        case CNL:
        case CDOL:
            if (*lp == 0)
                continue;
            return 0;

        case CEOF:
            loc2 = lp;
            return 1;

        case CCL:
            // No mask on c: CCLSIZE bytes hold every value a byte can take (sed.h).  v7
            // masked with 0177 here, which folded 0320 -- the lead byte of a Cyrillic
            // capital -- onto `P', so a class asked for one letter and matched another.
            c = *lp++;
            if (ep[c >> 3] & bittab[c & 07]) {
                ep += CCLSIZE;
                continue;
            }
            return 0;

        case CBRA:
            braslist[*ep++] = lp;
            continue;

        case CKET:
            braelist[*ep++] = lp;
            continue;

        case CBACK:
            bbeg = braslist[*ep];
            ct   = braelist[*ep++] - bbeg;

            if (ecmp(bbeg, lp, ct)) {
                lp += ct;
                continue;
            }
            return 0;

        case CBACK | STAR:
            bbeg  = braslist[*ep];
            ct    = braelist[*ep++] - bbeg;
            curlp = lp;
            while (ecmp(bbeg, lp, ct))
                lp += ct;

            while (lp >= curlp) {
                if (advance(lp, ep))
                    return 1;
                lp -= ct;
            }
            return 0;

        case CDOT | STAR:
            curlp = lp;
            while (*lp++)
                ;
            goto star;

        case CCHR | STAR:
            curlp = lp;
            while (*lp++ == *ep)
                ;
            ep++;
            goto star;

        case CCL | STAR:
            curlp = lp;
            do {
                c = *lp++;
            } while (ep[c >> 3] & bittab[c & 07]);
            ep += CCLSIZE;
            goto star;

        star:
            if (--lp == curlp)
                continue;

            if (*ep == CCHR) {
                c = ep[1];
                do {
                    if (*lp != c)
                        continue;
                    if (advance(lp, ep))
                        return 1;
                } while (lp-- > curlp);
                return 0;
            }

            if (*ep == CBACK) {
                c = *(braslist[ep[1]]);
                do {
                    if (*lp != c)
                        continue;
                    if (advance(lp, ep))
                        return 1;
                } while (lp-- > curlp);
                return 0;
            }

            do {
                if (lp == locs)
                    break;
                if (advance(lp, ep))
                    return 1;
            } while (lp-- > curlp);
            return 0;

        default:
            // v7 printed this and fell back into the switch, so a corrupted opcode gave
            // an infinite stream of diagnostics rather than a stop -- and a corrupted
            // opcode is exactly the state the old character-class store left the arena in.
            fprintf(stderr, "RE botch, %o\n", *--ep);
            exit(2);
        }
}

static int substitute(struct reptr *ipc)
{
    if (match(ipc->u.re1, 0) == 0)
        return 0;

    sflag = 1;
    dosub(ipc->rhs);

    if (ipc->gfl) {
        while (*loc2) {
            if (match(ipc->u.re1, 1) == 0)
                break;
            dosub(ipc->rhs);
        }
    }
    return 1;
}

// Build the substituted line in genbuf and copy it back.  v7 held a char * cursor into
// genbuf and tested it against &genbuf[LBSIZE] three times without acting on the answer;
// an index is what can be bounded, which is ed(1)'s finding in the same routine.
static void dosub(char *rhsbuf)
{
    char *lp, *rp;
    int sn, c, esc;

    lp = linebuf;
    rp = rhsbuf;
    sn = 0;
    // The head of the line, up to the match.  Both cursors are inside linebuf, so sn
    // cannot reach LBSIZE here.
    while (lp < loc1)
        genbuf[sn++] = *lp++;
    while ((c = *rp++) != 0) {
        esc = 0;
        if (c == QESC) {
            c   = *rp++;
            esc = 1;
        }
        if (!esc && c == '&') {
            sn = place(sn, loc1, loc2);
            continue;
        }
        if (esc && c >= '1' && c < NBRA + '1') {
            sn = place(sn, braslist[c - '1'], braelist[c - '1']);
            continue;
        }
        // v7 wrote `c & 0177' here, which is where a replacement lost its eighth bit.
        genbuf[sn++] = c;
        if (sn >= LBSIZE)
            toolong();
    }
    lp   = loc2;
    loc2 = linebuf + sn;
    while ((genbuf[sn++] = *lp++))
        if (sn >= LBSIZE)
            toolong();
    lp = linebuf;
    sn = 0;
    while ((*lp++ = genbuf[sn++]))
        ;
    spend = lp - 1;
}

static int place(int sn, char *l1, char *l2)
{
    while (l1 < l2) {
        genbuf[sn++] = *l1++;
        if (sn >= LBSIZE)
            toolong();
    }
    return sn;
}

static void command(struct reptr *ipc)
{
    int i;
    char *p1, *p2, *p3;
    char *execp;

    switch (ipc->command) {
    case ACOM:
        // v7 stored first and warned afterwards, and the slot it warned about is the one
        // the NUL terminator needs -- so the twentieth append wrote abuf[ABUFSIZE] and
        // then carried on.
        if (aptr >= &abuf[ABUFSIZE - 1]) {
            fprintf(stderr, "Too many appends after line %d\n", lnum);
            exit(2);
        }
        *aptr++ = ipc;
        *aptr   = 0;
        break;

    case CCOM:
        delflag = 1;
        if (!ipc->inar || dolflag) {
            for (p1 = ipc->u.re1; *p1;)
                putc(*p1++, stdout);
            putc('\n', stdout);
        }
        break;

    case DCOM:
        delflag++;
        break;

    case CDCOM:
        p1 = p2 = linebuf;

        while (*p1 != '\n') {
            if (*p1++ == 0) {
                delflag++;
                return;
            }
        }

        p1++;
        while ((*p2++ = *p1++))
            ;
        spend = p2 - 1;
        jflag++;
        break;

    case EQCOM:
        fprintf(stdout, "%d\n", lnum);
        break;

    case GCOM:
        p1 = linebuf;
        p2 = holdsp;
        while ((*p1++ = *p2++))
            ;
        spend = p1 - 1;
        break;

    case CGCOM:
        *spend++ = '\n';
        p1       = spend;
        p2       = holdsp;
        while ((*p1++ = *p2++))
            if (p1 >= lbend)
                break;
        spend = p1 - 1;
        break;

    case HCOM:
        p1 = holdsp;
        p2 = linebuf;
        while ((*p1++ = *p2++))
            ;
        hspend = p1 - 1;
        break;

    case CHCOM:
        *hspend++ = '\n';
        p1        = hspend;
        p2        = linebuf;
        while ((*p1++ = *p2++))
            if (p1 >= hend)
                break;
        hspend = p1 - 1;
        break;

    case ICOM:
        for (p1 = ipc->u.re1; *p1;)
            putc(*p1++, stdout);
        putc('\n', stdout);
        break;

    case BCOM:
        jflag = 1;
        break;

    case LCOM:
        p1         = linebuf;
        p2         = genbuf;
        genbuf[72] = 0;
        while (*p1)
            // A byte above 0177 is not a control character here, a plain char being
            // unsigned (../README.md SS11), so it takes this arm and `l' passes UTF-8
            // through rather than spelling each of its bytes in octal.  ed(1)'s l does
            // the same; on the PDP-11 the same line printed \3 and friends, the octal of
            // a negative number.
            if (*p1 >= 040) {
                if (*p1 == 0177) {
                    p3 = rub;
                    while ((*p2++ = *p3++))
                        if (p2 >= lcomend) {
                            *p2 = '\\';
                            fprintf(stdout, "%s\n", genbuf);
                            p2 = genbuf;
                        }
                    p2--;
                    p1++;
                    continue;
                }
                *p2++ = *p1++;
                if (p2 >= lcomend) {
                    *p2 = '\\';
                    fprintf(stdout, "%s\n", genbuf);
                    p2 = genbuf;
                }
            } else {
                p3 = trans[*p1 - 1];
                while ((*p2++ = *p3++))
                    if (p2 >= lcomend) {
                        *p2 = '\\';
                        fprintf(stdout, "%s\n", genbuf);
                        p2 = genbuf;
                    }
                p2--;
                p1++;
            }
        *p2 = 0;
        fprintf(stdout, "%s\n", genbuf);
        break;

    case NCOM:
        if (!nflag) {
            for (p1 = linebuf; p1 < spend; p1++)
                putc(*p1, stdout);
            putc('\n', stdout);
        }

        if (aptr > abuf)
            arout();
        if ((execp = gline(linebuf)) == badp) {
            pending = ipc;
            delflag = 1;
            break;
        }
        spend = execp;

        break;

    case CNCOM:
        if (aptr > abuf)
            arout();
        *spend++ = '\n';
        if ((execp = gline(spend)) == badp) {
            pending = ipc;
            delflag = 1;
            break;
        }
        spend = execp;
        break;

    case PCOM:
        for (p1 = linebuf; p1 < spend; p1++)
            putc(*p1, stdout);
        putc('\n', stdout);
        break;

    case CPCOM:
    cpcom:
        for (p1 = linebuf; *p1 != '\n' && *p1 != '\0';)
            putc(*p1++, stdout);
        putc('\n', stdout);
        break;

    case QCOM:
        if (!nflag) {
            for (p1 = linebuf; p1 < spend; p1++)
                putc(*p1, stdout);
            putc('\n', stdout);
        }
        if (aptr > abuf)
            arout();
        fclose(stdout);
        exit(errflag ? 2 : 0);

    case RCOM:
        if (aptr >= &abuf[ABUFSIZE - 1]) {
            // v7 spelled this "after line%d", with no space.
            fprintf(stderr, "Too many reads after line %d\n", lnum);
            exit(2);
        }
        *aptr++ = ipc;
        *aptr   = 0;
        break;

    case SCOM:
        i = substitute(ipc);
        if (ipc->pfl && i) {
            if (ipc->pfl == 1) {
                for (p1 = linebuf; p1 < spend; p1++)
                    putc(*p1, stdout);
                putc('\n', stdout);
            } else
                goto cpcom;
        }
        if (i && ipc->fcode)
            goto wcom;
        break;

    case TCOM:
        if (sflag == 0)
            break;
        sflag = 0;
        jflag = 1;
        break;

    wcom:
    case WCOM:
        fprintf(ipc->fcode, "%s\n", linebuf);
        break;

    case XCOM:
        p1 = linebuf;
        p2 = genbuf;
        while ((*p2++ = *p1++))
            ;
        p1 = holdsp;
        p2 = linebuf;
        while ((*p2++ = *p1++))
            ;
        spend = p2 - 1;
        p1    = genbuf;
        p2    = holdsp;
        while ((*p2++ = *p1++))
            ;
        hspend = p2 - 1;
        break;

    case YCOM:
        // The table is YSIZE entries now, so this subscript is in range for every byte a
        // line can hold.  v7's was 128 and this line indexed it unmasked.
        p1 = linebuf;
        p2 = ipc->u.re1;
        while ((*p1 = p2[*(unsigned char *)p1]))
            p1++;
        break;
    }
}

static char *gline(char *addr)
{
    char *p1, *p2;
    int c;

    p1 = addr;
    p2 = cbp;
    for (;;) {
        if (p2 >= ebp) {
            if ((c = read(infile, ibuf, sizeof ibuf)) <= 0)
                return badp;
            p2  = ibuf;
            ebp = ibuf + c;
        }
        if ((c = *p2++) == '\n') {
            if (p2 >= ebp) {
                if ((c = read(infile, ibuf, sizeof ibuf)) <= 0) {
                    close(infile);
                    if (eargc == 0)
                        dolflag = 1;
                    // v7 left the read's -1 in c and formed ibuf - 1, a pointer before
                    // its own array.
                    c = 0;
                }
                p2  = ibuf;
                ebp = ibuf + c;
            }
            break;
        }
        if (c) {
            // v7 dropped every byte past the end of the pattern space in silence, so a
            // long line came out short and nothing said so.
            if (p1 >= lbend) {
                fprintf(stderr, "sed: input line too long\n");
                exit(2);
            }
            *p1++ = c;
        }
    }
    lnum++;
    *p1 = 0;
    cbp = p2;

    return p1;
}

static int ecmp(char *a, char *b, int count)
{
    while (count--)
        if (*a++ != *b++)
            return 0;
    return 1;
}

static void arout(void)
{
    char *p1;
    FILE *fi;
    int i, t;

    // v7 walked this with `aptr = abuf - 1; while (*++aptr)', forming a pointer before
    // the array.
    for (i = 0; abuf[i]; i++) {
        if (abuf[i]->command == ACOM) {
            for (p1 = abuf[i]->u.re1; *p1;)
                putc(*p1++, stdout);
            putc('\n', stdout);
        } else {
            if ((fi = fopen(abuf[i]->u.re1, "r")) == NULL)
                continue;
            while ((t = getc(fi)) != EOF)
                putc(t, stdout);
            fclose(fi);
        }
    }
    aptr  = abuf;
    *aptr = 0;
}
