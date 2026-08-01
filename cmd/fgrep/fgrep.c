/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * fgrep -- print all lines containing any of a set of keywords
 *
 *	status returns:
 *		0 - ok, and some matches
 *		1 - ok, but no matches
 *		2 - some error
 *
 * Task C5c, with grep(1).  ../grep/README.md is the account and ../grep/grep.1
 * the manual page -- v7 documented all three commands of the family on one page
 * and this port keeps that.  Two things a reader of the diff needs:
 *
 * MAXSIZ IS 3000, not v7's 6000.  This is an Aho-Corasick machine and w[] is its
 * state table, one `struct words' per state, and a state is FOUR WORDS here --
 * measured, not assumed: the two adjacent char members share one word and the
 * three pointers take one each, where the PDP-11 packed the lot into eight
 * bytes.  So v7's 6000 states are 24,000 words of bss, and the whole user
 * address space is 28,672 (../README.md SS6): fgrep did not fit before a byte of
 * text.  3000 states is 12,000 words -- the program measures 17,048 all told --
 * and is about 3,000 bytes of keyword text, far past any list anybody types;
 * overflo() below has always diagnosed the limit, so the smaller table fails
 * loudly rather than quietly.  The _Static_asserts make a later change to the
 * struct or the count break the BUILD rather than the image, which is
 * ../README.md's rule for anything that encodes a layout.  This is icheck(1)'s
 * finding from task C4e: a fixed table is a ceiling somebody chose against
 * different hardware, so ask what bounds it here before keeping the number.
 *
 * -b IS A BYTE OFFSET.  v7 printed (blkno-ccount-1)/512, a PDP-11 disk block;
 * grep.c divided by BSIZE instead, so the two commands printed different numbers
 * for the same match.  See the header of ../grep/grep.c for why the division
 * goes rather than the divisor changing.  The offset is of the START of the
 * matching line, as grep's is.
 */

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>
#include <unistd.h>

#define MAXSIZ 3000
#define QSIZE  400

// The input ring: two halves, read into alternately.  512 is this program's own
// I/O chunking and not a unit it reports, so ../README.md SS4 leaves it alone.
// It is also this program's line-length limit, and the limit is BUFF and not
// HALF: nlp holds the start of the line being scanned, and the head is lost only
// when the reader laps it.  Measured -- a 1,025-byte matching line prints one
// byte -- because grep.1's first draft said 512 from a reading of the code.
#define HALF 512
#define BUFF (2 * HALF)

struct words {
    char inp;
    char out;
    struct words *nst;
    struct words *link;
    struct words *fail;
};

static struct words w[MAXSIZ], *smax, *q;

// Four words a state today, measured with b6size.  The bound is written as five --
// doc/Besm6_Data_Representation.md SS8's worst case, a word per scalar member -- so
// that this fails on a real growth of the struct rather than on a repacking; the
// budget below is what actually holds the table inside the address space.
_Static_assert(sizeof(struct words) <= 5 * NBPW,
               "struct words grew: re-derive MAXSIZ against cmd/README.md SS6");
_Static_assert(MAXSIZ * sizeof(struct words) <= 15000 * NBPW,
               "w[] is past its 15,000-word budget; see the header");

static int lnum;
static int bflag, cflag, fflag, lflag, nflag, vflag, xflag, yflag;
static int hflag = 1;
static int sflag;
static int retcode = 0;
static int nfile;
static int nsucc;
static int tln;
static FILE *wordf;
static char *argptr;

// The byte offset of the first byte of the line being scanned, and the running
// count -b reports from.  v7 kept blkno, a count of bytes read, and divided it.
static int loff;
static int coff;

static void execute(char *file);
static int getargc(void);
static void cgotofn(void);
static void overflo(void);
static void cfail(void);

// A byte above 0177 is part of a multi-byte letter, which has no case here, so
// -y leaves it alone; lib/libc/gen/ctype_.c is 129 entries and would be read off
// the end without the guard (../README.md SS11).  look(1)'s rule from task C5b.
//
// lca() is a function where v7 had a macro, so that the ctype work stays out of
// line; ccomp() stays a macro, because it is the inner loop of the whole program
// and its common arm -- yflag unset -- is one comparison.  Neither call site
// passes an argument with a side effect, which is what the double evaluation
// would otherwise cost.
static int lca(int x)
{
    return (isascii(x) && isupper(x)) ? tolower(x) : x;
}

#define ccomp(a, b) (yflag ? lca(a) == lca(b) : (a) == (b))

int main(int argc, char **argv)
{
    while (--argc > 0 && (++argv)[0][0] == '-')
        switch (argv[0][1]) {
        case 's':
            sflag++;
            continue;

        case 'h':
            hflag = 0;
            continue;

        case 'b':
            bflag++;
            continue;

        case 'c':
            cflag++;
            continue;

        case 'e':
            argc--;
            argv++;
            goto out;

        case 'f':
            fflag++;
            continue;

        case 'l':
            lflag++;
            continue;

        case 'n':
            nflag++;
            continue;

        case 'v':
            vflag++;
            continue;

        case 'x':
            xflag++;
            continue;

        case 'y':
            yflag++;
            continue;
        default:
            // v7 went on round the loop without consuming the argument, which
            // cannot terminate on `fgrep --', and then searched for whatever
            // the bad flag happened to be.  A bad flag is an error now, as it
            // is in grep(1), and the status is the 2 the manual page promises.
            fprintf(stderr, "fgrep: unknown flag\n");
            exit(2);
        }
out:
    if (argc <= 0) {
        fprintf(stderr, "usage: fgrep [-bchlnsvxy] [-e] [-f file] strings [file] ...\n");
        exit(2);
    }
    if (fflag) {
        wordf = fopen(*argv, "r");
        if (wordf == NULL) {
            fprintf(stderr, "fgrep: can't open %s\n", *argv);
            exit(2);
        }
    } else
        argptr = *argv;
    argc--;
    argv++;

    cgotofn();
    cfail();
    nfile = argc;
    if (argc <= 0) {
        if (lflag)
            exit(1);
        execute((char *)NULL);
    } else
        while (--argc >= 0) {
            execute(*argv);
            argv++;
        }
    exit(retcode != 0 ? retcode : nsucc == 0);
}

static void execute(char *file)
{
    struct words *c;
    int ccount;
    char *p;
    char buf[BUFF]; // 171 words of the 4,096 the stack has (../README.md SS6)
    int f;
    int failed;
    char *nlp;

    if (file) {
        if ((f = open(file, O_RDONLY)) < 0) {
            fprintf(stderr, "fgrep: can't open %s\n", file);
            retcode = 2;
            return;
        }
    } else
        f = 0;
    ccount = 0;
    failed = 0;
    lnum   = 1;
    tln    = 0;
    coff   = 0;
    loff   = 0;
    p      = buf;
    nlp    = p;
    c      = w;
    for (;;) {
        if (--ccount <= 0) {
            if (p == &buf[BUFF])
                p = buf;
            if (p > &buf[HALF]) {
                if ((ccount = read(f, p, &buf[BUFF] - p)) <= 0)
                    break;
            } else if ((ccount = read(f, p, HALF)) <= 0)
                break;
        }
    nstate:
        if (ccomp(c->inp, *p)) {
            c = c->nst;
        } else if (c->link != 0) {
            c = c->link;
            goto nstate;
        } else {
            c      = c->fail;
            failed = 1;
            if (c == 0) {
                c = w;
            istate:
                if (ccomp(c->inp, *p)) {
                    c = c->nst;
                } else if (c->link != 0) {
                    c = c->link;
                    goto istate;
                }
            } else
                goto nstate;
        }
        if (c->out) {
            while (*p++ != '\n') {
                coff++;
                if (--ccount <= 0) {
                    if (p == &buf[BUFF])
                        p = buf;
                    if (p > &buf[HALF]) {
                        if ((ccount = read(f, p, &buf[BUFF] - p)) <= 0)
                            break;
                    } else if ((ccount = read(f, p, HALF)) <= 0)
                        break;
                }
            }
            coff++;
            if ((vflag && (failed == 0 || xflag == 0)) || (vflag == 0 && xflag && failed))
                goto nomatch;
        succeed:
            nsucc = 1;
            if (cflag)
                tln++;
            else if (sflag)
                ; /* ugh */
            else if (lflag) {
                printf("%s\n", file);
                close(f);
                return;
            } else {
                if (nfile > 1 && hflag)
                    printf("%s:", file);
                if (bflag)
                    printf("%d:", loff); // the byte offset of the line
                if (nflag)
                    printf("%d:", lnum);
                if (p <= nlp) {
                    while (nlp < &buf[BUFF])
                        putchar(*nlp++);
                    nlp = buf;
                }
                while (nlp < p)
                    putchar(*nlp++);
            }
        nomatch:
            lnum++;
            nlp    = p;
            loff   = coff;
            c      = w;
            failed = 0;
            continue;
        }
        coff++;
        if (*p++ == '\n') {
            if (vflag)
                goto succeed;
            else {
                lnum++;
                nlp    = p;
                loff   = coff;
                c      = w;
                failed = 0;
            }
        }
    }
    close(f);
    if (cflag) {
        if (nfile > 1 && hflag)
            printf("%s:", file);
        printf("%d\n", tln);
    }
}

static int getargc(void)
{
    int c;
    if (wordf)
        return (getc(wordf));
    if ((c = *argptr++) == '\0')
        return (EOF);
    return (c);
}

static void cgotofn(void)
{
    int c;
    struct words *s;

    s = smax = w;
nword:
    for (;;) {
        c = getargc();
        if (c == EOF)
            return;
        if (c == '\n') {
            if (xflag) {
                for (;;) {
                    if (s->inp == c) {
                        s = s->nst;
                        break;
                    }
                    if (s->inp == 0)
                        goto nenter;
                    if (s->link == 0) {
                        if (smax >= &w[MAXSIZ - 1])
                            overflo();
                        s->link = ++smax;
                        s       = smax;
                        goto nenter;
                    }
                    s = s->link;
                }
            }
            s->out = 1;
            s      = w;
        } else {
        loop:
            if (s->inp == c) {
                s = s->nst;
                continue;
            }
            if (s->inp == 0)
                goto enter;
            if (s->link == 0) {
                if (smax >= &w[MAXSIZ - 1])
                    overflo();
                s->link = ++smax;
                s       = smax;
                goto enter;
            }
            s = s->link;
            goto loop;
        }
    }

enter:
    do {
        s->inp = c;
        if (smax >= &w[MAXSIZ - 1])
            overflo();
        s->nst = ++smax;
        s      = smax;
    } while ((c = getargc()) != '\n' && c != EOF);
    if (xflag) {
    nenter:
        s->inp = '\n';
        if (smax >= &w[MAXSIZ - 1])
            overflo();
        s->nst = ++smax;
    }
    smax->out = 1;
    s         = w;
    if (c != EOF)
        goto nword;
}

static void overflo(void)
{
    fprintf(stderr, "fgrep: wordlist too large\n");
    exit(2);
}

static void cfail(void)
{
    struct words *queue[QSIZE]; // 400 words of the 4,096 the stack has
    struct words **front, **rear;
    struct words *state;
    int bstart;
    int c;
    struct words *s;

    s     = w;
    front = rear = queue;
init:
    if ((s->inp) != 0) {
        *rear++ = s->nst;
        if (rear >= &queue[QSIZE - 1])
            overflo();
    }
    if ((s = s->link) != 0) {
        goto init;
    }

    while (rear != front) {
        s = *front;
        if (front == &queue[QSIZE - 1])
            front = queue;
        else
            front++;
    cloop:
        if ((c = s->inp) != 0) {
            bstart = 0;
            *rear  = (q = s->nst);
            if (front < rear) {
                if (rear >= &queue[QSIZE - 1]) {
                    if (front == queue)
                        overflo();
                    else
                        rear = queue;
                } else
                    rear++;
            } else if (++rear == front)
                overflo();
            state = s->fail;
        floop:
            if (state == 0) {
                state  = w;
                bstart = 1;
            }
            if (state->inp == c) {
            qloop:
                q->fail = state->nst;
                if ((state->nst)->out == 1)
                    q->out = 1;
                if ((q = q->link) != 0)
                    goto qloop;
            } else if ((state = state->link) != 0)
                goto floop;
            else if (bstart == 0) {
                state = 0;
                goto floop;
            }
        }
        if ((s = s->link) != 0)
            goto cloop;
    }
}
