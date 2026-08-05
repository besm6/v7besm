/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * grep -- print lines matching (or not matching) a pattern
 *
 *	status returns:
 *		0 - ok, and some matches
 *		1 - ok, but no matches
 *		2 - some error
 *
 * Task C5c.  See README.md beside this file; what follows is only what a reader
 * of the diff needs.
 *
 * THE CHARACTER CLASS IS 256 BITS, not v7's 128.  A class is a bitmap laid into
 * expbuf after the CCL opcode, indexed `ep[c >> 3] & bittab[c & 07]'.  Sixteen
 * bytes hold c in 0..0177 and this machine's text is UTF-8 (../README.md SS11), so
 * the table is thirty-two bytes and every byte value has a bit of its own.  That
 * closed a wild store as well as adding the feature: the COMPILE side never
 * masked c at all, so a pattern byte of 0300 stored at ep[24] -- eight bytes past
 * a sixteen-byte class, into whatever bytecode followed it.  With 32 bytes
 * c >> 3 lands in 0..31 by construction and no mask is needed anywhere.
 *
 * -b IS A BYTE OFFSET, which is a deliberate divergence from v7 and is written
 * down here, in grep.1.umm and in README.md as ../README.md SS10 requires.  v7 printed a
 * block number: ftell()/BSIZE, where BSIZE was a PDP-11 disk block of 512 bytes.
 * 512 names nothing on this machine (../README.md SS4), BSIZE here is 3072 and would
 * name this filesystem's blocking rather than a position, and fgrep hard-coded
 * 512 where this file used BSIZE -- so the two commands printed different
 * numbers for the same match.  A byte offset is exact, needs no unit, is what
 * every later Unix means by -b, and it removes the disagreement by deleting the
 * division rather than by choosing a divisor.  It counts bytes rather than using
 * ftell(), so it works when standard input is a pipe.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CBRA  1
#define CCHR  2
#define CDOT  4
#define CCL   6
#define NCCL  8
#define CDOL  10
#define CEOF  11
#define CKET  12
#define CBACK 18

#define STAR 01

#define LBSIZE 512

// ESIZE was 128 in v7 and is 512 here, for ed(1)'s two reasons (cmd/ed/README.md):
// a class costs 32 bytes now rather than 16, and a UTF-8 letter in a pattern costs
// four -- CCHR plus the byte, twice.  512 bytes is 86 words.
#define ESIZE 512
#define NBRA  9

// CCLSIZE is the width of one class, in bytes: 256 bits, one per byte value.
// Changing it means changing nothing else -- compile(), advance() and the room
// check below are all written in terms of it.
#define CCLSIZE 32

static char expbuf[ESIZE];
static int lnum;
static char linebuf[LBSIZE + 1];
static char ybuf[ESIZE];
static int bflag;
static int lflag;
static int nflag;
static int cflag;
static int vflag;
static int nfile;
static int hflag = 1;
static int sflag;
static int yflag;
static int circf;
static int tln;
static int nsucc;

// The byte offset of the first byte of linebuf within the current file, and the
// running count -b reports from.  v7 asked ftell(); a pipe cannot answer that.
static int loff;
static int coff;

static char *braslist[NBRA];
static char *braelist[NBRA];
static char bittab[] = { 1, 2, 4, 8, 16, 32, 64, 128 };

// advance() recurses once per star operator that consumes something, so its depth is
// bounded by the number of stars in the compiled expression -- not, as it looks, by the
// length of the line.  The stack is 4,096 words at 070000 and NOTHING CHECKS IT
// (../README.md SS6), and b6disasm puts one advance() frame at 153 words, so about
// twenty-six of them fill it.  Measured, because the failure is not the one a reader
// expects: `grep "a*b*a*b*..." ' over a matching line returns the WRONG ANSWER -- an empty
// one -- at a depth of about forty, and only faults the machine at about fifty.  A silent
// wrong answer is what MAXDEPTH exists to prevent; a real expression recurses two or three
// deep, so nothing anybody writes comes near it.
#define MAXDEPTH 16
static int depth;

static void compile(char *astr);
static void execute(char *file);
static int advance(char *lp, char *ep);
static int match(char *lp, char *ep);
static void succeed(char *f);
static int ecmp(char *a, char *b, int count);
static void errexit(char *s, char *f);

int main(int argc, char **argv)
{
    while (--argc > 0 && (++argv)[0][0] == '-')
        switch (argv[0][1]) {
        case 'y':
            yflag++;
            continue;

        case 'h':
            hflag = 0;
            continue;

        case 's':
            sflag++;
            continue;

        case 'v':
            vflag++;
            continue;

        case 'b':
            bflag++;
            continue;

        case 'l':
            lflag++;
            continue;

        case 'c':
            cflag++;
            continue;

        case 'n':
            nflag++;
            continue;

        case 'e':
            --argc;
            ++argv;
            goto out;

        default:
            // v7 printed the diagnostic and went on round the loop without
            // consuming the argument, which cannot terminate on `grep --'.
            // errexit() exits, so the `continue' it wrote here was dead code.
            errexit("grep: unknown flag\n", (char *)NULL);
        }
out:
    if (argc <= 0) {
        // v7 exited 2 in silence.  A usage line costs nothing and the exit
        // status is unchanged, so no script can tell the difference.
        fprintf(stderr, "usage: grep [-bchlnsvy] [-e] expression [file] ...\n");
        exit(2);
    }
    if (yflag) {
        char *p, *s;
        for (s = ybuf, p = *argv; *p;) {
            if (*p == '\\') {
                *s++ = *p++;
                if (*p)
                    *s++ = *p++;
            } else if (*p == '[') {
                while (*p != '\0' && *p != ']')
                    *s++ = *p++;
            } else if (isascii(*p) && islower(*p)) {
                // isascii() because lib/libc/gen/ctype_.c is 129 entries and
                // *p is a byte the caller chose (../README.md SS11).  A byte
                // above 0177 is part of a multi-byte letter, which has no case
                // here, so -y copies it through unchanged.  look(1)'s rule.
                *s++ = '[';
                *s++ = toupper(*p);
                *s++ = *p++;
                *s++ = ']';
            } else
                *s++ = *p++;
            if (s >= ybuf + ESIZE - 5)
                errexit("grep: argument too long\n", (char *)NULL);
        }
        *s    = '\0';
        *argv = ybuf;
    }
    compile(*argv);
    nfile = --argc;
    if (argc <= 0) {
        if (lflag)
            exit(1);
        execute((char *)NULL);
    } else
        while (--argc >= 0) {
            argv++;
            execute(*argv);
        }
    exit(nsucc == 0);
}

static void compile(char *astr)
{
    int c;
    char *ep, *sp;
    char *cstart;
    char *lastep;
    int cclcnt;
    char bracket[NBRA], *bracketp;
    int closed;
    int numbra;
    int neg;

    ep       = expbuf;
    sp       = astr;
    lastep   = 0;
    bracketp = bracket;
    closed = numbra = 0;
    if (*sp == '^') {
        circf++;
        sp++;
    }
    for (;;) {
        if (ep >= &expbuf[ESIZE])
            goto cerror;
        if ((c = *sp++) != '*')
            lastep = ep;
        switch (c) {
        case '\0':
            *ep++ = CEOF;
            return;

        case '.':
            *ep++ = CDOT;
            continue;

        case '*':
            if (lastep == 0 || *lastep == CBRA || *lastep == CKET)
                goto defchar;
            *lastep |= STAR;
            continue;

        case '$':
            if (*sp != '\0')
                goto defchar;
            *ep++ = CDOL;
            continue;

        case '[':
            if (&ep[CCLSIZE + 1] >= &expbuf[ESIZE])
                goto cerror;
            *ep++ = CCL;
            memset(ep, 0, CCLSIZE);
            neg = 0;
            if ((c = *sp++) == '^') {
                neg = 1;
                c   = *sp++;
            }
            cstart = sp;
            do {
                if (c == '\0')
                    goto cerror;
                if (c == '-' && sp > cstart && *sp != ']') {
                    // A range is a range of BYTES, as ed(1)'s classes are, so a
                    // range written between two multi-byte letters is not what a
                    // reader expects.  grep.1.umm says so; see cmd/ed/README.md.
                    for (c = sp[-2]; c < *sp; c++)
                        ep[c >> 3] |= bittab[c & 07];
                    sp++;
                }
                // No mask on c: CCLSIZE bytes hold every value a byte can take.
                ep[c >> 3] |= bittab[c & 07];
            } while ((c = *sp++) != ']');
            if (neg) {
                for (cclcnt = 0; cclcnt < CCLSIZE; cclcnt++)
                    ep[cclcnt] ^= 0377; // v7 wrote ^= -1, into a signed char
                ep[0] &= 0376;          // a negated class never matches the terminator
            }

            ep += CCLSIZE;

            continue;

        case '\\':
            if ((c = *sp++) == '(') {
                if (numbra >= NBRA) {
                    goto cerror;
                }
                *bracketp++ = numbra;
                *ep++       = CBRA;
                *ep++       = numbra++;
                continue;
            }
            if (c == ')') {
                if (bracketp <= bracket) {
                    goto cerror;
                }
                *ep++ = CKET;
                *ep++ = *--bracketp;
                closed++;
                continue;
            }

            if (c >= '1' && c <= '9') {
                if ((c -= '1') >= closed)
                    goto cerror;
                *ep++ = CBACK;
                *ep++ = c;
                continue;
            }

        defchar:
        default:
            *ep++ = CCHR;
            *ep++ = c;
        }
    }
cerror:
    errexit("grep: RE error\n", (char *)NULL);
}

static void execute(char *file)
{
    char *p1, *p2;
    int c;

    if (file) {
        if (freopen(file, "r", stdin) == NULL)
            errexit("grep: can't open %s\n", file);
    }
    lnum = 0;
    tln  = 0;
    coff = 0;
    for (;;) {
        lnum++;
        loff = coff;
        p1   = linebuf;
        while ((c = getchar()) != '\n') {
            if (c == EOF) {
                if (cflag) {
                    if (nfile > 1 && hflag)
                        printf("%s:", file);
                    // v7 wrote %D, which is not a conversion here: doprnt()
                    // echoes an unknown one verbatim and consumes no argument
                    // (../README.md SS3), so `grep -c' printed the two characters
                    // %D.  It also honours -h now, as the ordinary path does.
                    printf("%d\n", tln);
                }
                return;
            }
            coff++;
            *p1++ = c;
            if (p1 >= &linebuf[LBSIZE - 1])
                break;
        }
        if (c == '\n')
            coff++;
        *p1++ = '\0';
        p1    = linebuf;
        p2    = expbuf;
        if (circf) {
            if (advance(p1, p2))
                goto found;
            goto nfound;
        }
        /* fast check for first character */
        if (*p2 == CCHR) {
            c = p2[1];
            do {
                if (*p1 != c)
                    continue;
                if (advance(p1, p2))
                    goto found;
            } while (*p1++);
            goto nfound;
        }
        /* regular algorithm */
        do {
            if (advance(p1, p2))
                goto found;
        } while (*p1++);
    nfound:
        if (vflag)
            succeed(file);
        continue;
    found:
        if (vflag == 0)
            succeed(file);
    }
}

// The counted wrapper.  match() is v7's advance() unchanged but for the three places it
// calls itself, which come back through here so that every level is counted.
static int advance(char *lp, char *ep)
{
    int r;

    if (++depth > MAXDEPTH)
        errexit("grep: expression too complex\n", (char *)NULL);
    r = match(lp, ep);
    depth--;
    return r;
}

static int match(char *lp, char *ep)
{
    char *curlp;
    int c;
    char *bbeg;
    int ct;

    for (;;)
        switch (*ep++) {
        case CCHR:
            if (*ep++ == *lp++)
                continue;
            return (0);

        case CDOT:
            if (*lp++)
                continue;
            return (0);

        case CDOL:
            if (*lp == 0)
                continue;
            return (0);

        case CEOF:
            return (1);

        case CCL:
            c = *lp++; // v7 masked with 0177 into a 128-bit table
            if (ep[c >> 3] & bittab[c & 07]) {
                ep += CCLSIZE;
                continue;
            }
            return (0);
        case CBRA:
            braslist[*ep++] = lp;
            continue;

        case CKET:
            braelist[*ep++] = lp;
            continue;

        case CBACK:
            bbeg = braslist[*ep];
            if (braelist[*ep] == 0)
                return (0);
            ct = braelist[*ep++] - bbeg;
            if (ecmp(bbeg, lp, ct)) {
                lp += ct;
                continue;
            }
            return (0);

        case CBACK | STAR:
            bbeg = braslist[*ep];
            if (braelist[*ep] == 0)
                return (0);
            ct    = braelist[*ep++] - bbeg;
            curlp = lp;
            while (ecmp(bbeg, lp, ct))
                lp += ct;
            while (lp >= curlp) {
                if (advance(lp, ep))
                    return (1);
                lp -= ct;
            }
            return (0);

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
            if (--lp == curlp) {
                continue;
            }

            if (*ep == CCHR) {
                c = ep[1];
                do {
                    if (*lp != c)
                        continue;
                    if (advance(lp, ep))
                        return (1);
                } while (lp-- > curlp);
                return (0);
            }

            do {
                if (advance(lp, ep))
                    return (1);
            } while (lp-- > curlp);
            return (0);

        default:
            errexit("grep RE botch\n", (char *)NULL);
        }
}

static void succeed(char *f)
{
    nsucc = 1;
    if (sflag)
        return;
    if (cflag) {
        tln++;
        return;
    }
    if (lflag) {
        printf("%s\n", f);
        fseek(stdin, 0, 2);
        return;
    }
    if (nfile > 1 && hflag)
        printf("%s:", f);
    if (bflag)
        printf("%d:", loff); // the byte offset of the line; see the header
    if (nflag)
        printf("%d:", lnum);
    printf("%s\n", linebuf);
}

static int ecmp(char *a, char *b, int count)
{
    int cc = count;
    while (cc--)
        if (*a++ != *b++)
            return (0);
    return (1);
}

static void errexit(char *s, char *f)
{
    fprintf(stderr, s, f);
    exit(2);
}
