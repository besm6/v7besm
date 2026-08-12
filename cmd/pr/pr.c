/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// pr -- print files with headings, in one or more columns.
//
//      pr [ -n ] [ +n ] [ -h header ] [ -wn ] [ -ln ] [ -t ] [ -sc ] [ -m ] [ file ] ...
//
// The first of task C5f's seven (../README.md), and the last of the four "deciding" programs
// ../README.md §2 has been counting byte cursors in.  README.md for the account; what is
// worth having at the head of the source is why the two odd-looking things here are right.
//
// THE RING BUFFER IS THE PROGRAM.  Multi-column output has to read ahead: column 2 of a page
// is text that comes after column 1's, so pr holds up to 72 read cursors inside one circular
// buffer that it refills 512 bytes at a time.  v7 held the cursors as `char *' and this port
// holds them as int OFFSETS -- not for §2's sake, the compiler orders two byte pointers
// correctly since 2026-06-17, but because every one of these comparisons runs ONCE PER BYTE
// and a fat-pointer relational is two out-of-line calls (b$pdiff then b$lt) where an int test
// is a register compare.  That is sort(1)'s C5d treatment for the same reason.
//
// TWO SENTINELS LIVE IN THE CHARACTER STREAM, AND THEY DID NOT HAVE TO CHANGE.  0375 marks
// the refill point and 0376 the end of the file, both stored as ordinary bytes in the ring
// and read back through `& 0377'.  That is §11's third and worst shape -- a program that
// steals a byte value -- and it is exactly what col(1) had to have deleted.  It survives here
// because 0375 and 0376 are 0xFD and 0xFE, and **no valid UTF-8 sequence contains either**:
// the lead bytes stop at 0xF4 and continuation bytes at 0xBF.  So a Cyrillic file goes
// through this ring unharmed, and the reason is a property of UTF-8 rather than a property of
// pr.  A diff cannot show that, which is why it is here.
//
// The backward wrap in print() said `colp[ncol] = &buffer[BUFS]', one past the ring, and the
// next tpgetc() read that byte.  It is BUFS-1 now.
//
// AND THE RING HAD AN INVARIANT NOBODY HAD WRITTEN DOWN: a refill must not run over a cursor
// that has not read its bytes yet.  Nothing in v7 checked it, and nothing had to, because the
// only way to reach it is to ask for more columns than 6720 bytes can carry -- which `pr -72'
// on a wide page does.  What it produced was silently rearranged output.  nexbuf() measures
// the distance from the write cursor to each live read cursor now and refuses (§6's rule that
// a bound test which is not on every path reads exactly like one).
//
// pr -m IS A SECOND PROGRAM SHARING ONE FUNCTION: tpgetc() branches on mflg to a getc() path
// that never touches the ring at all.  Both halves are tested.
//
// THE TERMINAL chmod STAYS.  fixtty() takes the tty to 0600 for the duration so that write(1)
// cannot interleave a message into a listing, and done()/onintr() put the mode back.  Unlike
// col(1)'s Model 37 half-shift there is still a producer for this -- write(1) and mesg(1) are
// task C6 -- so the mechanism is kept rather than cut.  Under b6sim the terminal is the BUILD
// MACHINE's, so no case may run with standard output on a tty; every one redirects, which
// makes ttyname(1) answer NULL and the whole path a no-op.
//
// The header was sprintf'd from an unbounded -h argument into char[150] (§6's recurring
// finding, and the sixth port in a row to have one); mopen() drove nofile negative on a file
// it could not open and tested its ceiling after storing past it; and ncol could reach the
// `width/ncol' divide as zero.  All three are fixed.
//
// NOT SETUID: it opens what the caller could open itself, and chmods only its own terminal.
//
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define BUFS   6720 // the ring every column cursor reads from
#define MAXCOL 72   // as many cursors as there are columns, one per column
#define REFILL 0375 // in-band: the ring wants refilling at this position
#define ENDF   0376 // in-band: the input ended here
#define FF     014

static int ncol = 1;
static const char *header;
static int col;
static int icol;
static FILE *file;
static int bufp; // write cursor: an OFFSET into buffer, not a pointer
static char buffer[BUFS];
static char obuf[BUFSIZ];
static int line;
static int colp[MAXCOL]; // read cursors, offsets into buffer
static int nofile;
static char isclosed[10];
static FILE *ifile[10];
static char **lastarg;
static int peekc;
static int fpage;
static int page;
static int colw;
static int nspace;
static int width  = 72;
static int length = 66;
static int plength = 61;
static int margin = 10;
static int ntflg;
static int mflg;
static int tabc;
static char *tty;
static int mode;

static _Noreturn void done(void);
static void onintr(int sig);
static void fixtty(void);
static void print(const char *fp, char **argp);
static void mopen(char **ap);
static void putpage(void);
static void nexbuf(void);
static int tpgetc(int ai);
static int pgetc(int i);
static void put(int ac);
static void putcp(int c);

int main(int argc, char **argv)
{
    int nfdone;

    setbuf(stdout, obuf);
    if (signal(SIGINT, SIG_IGN) != SIG_IGN)
        signal(SIGINT, onintr);
    lastarg = &argv[argc - 1];
    fixtty();
    for (nfdone = 0; argc > 1; argc--) {
        argv++;
        if (**argv == '-') {
            switch (*++*argv) {
            case 'h':
                if (argc >= 2) {
                    header = *++argv;
                    argc--;
                }
                continue;

            case 't':
                ntflg++;
                continue;

            case 'l':
                length = atoi(++*argv);
                continue;

            case 'w':
                width = atoi(++*argv);
                continue;

            case 's':
                if (*++*argv)
                    tabc = **argv;
                else
                    tabc = '\t';
                continue;

            case 'm':
                mflg++;
                continue;

            default:
                ncol = atoi(*argv);
                continue;
            }
        } else if (**argv == '+') {
            fpage = atoi(++*argv);
        } else {
            print(*argv, argv);
            nfdone++;
            if (mflg)
                break;
        }
    }
    if (nfdone == 0)
        print(NULL, NULL);
    done();
}

static _Noreturn void done(void)
{
    if (tty)
        chmod(tty, mode);
    exit(0);
}

static void onintr(int sig)
{
    (void)sig;
    if (tty)
        chmod(tty, mode);
    _exit(1);
}

//
// Take our own terminal to 0600 so that write(1) cannot interleave a message into the
// listing.  Under b6sim standard output is always a redirection, so ttyname() answers NULL
// and nothing here runs; see the head of this file.
//
static void fixtty(void)
{
    struct stat sbuf;

    tty = ttyname(1);
    if (tty == NULL)
        return;
    if (stat(tty, &sbuf) < 0) {
        tty = NULL;
        return;
    }
    mode = sbuf.st_mode & 0777;
    chmod(tty, 0600);
}

static void print(const char *fp, char **argp)
{
    struct stat sbuf;
    int sncol;
    const char *sheader;
    char *cbuf;
    char linebuf[150];
    const char *cp;
    time_t stamp;

    if (ntflg)
        margin = 0;
    else
        margin = 10;
    if (length <= margin)
        length = 66;
    if (width <= 0)
        width = 72;
    // v7 tested only the two upper bounds, so `pr -x' -- atoi of a letter -- reached the
    // width/ncol divide with a zero divisor.
    if (ncol < 1 || ncol > MAXCOL || ncol > width) {
        fprintf(stderr, "pr: No room for columns.\n");
        done();
    }
    if (mflg) {
        mopen(argp);
        ncol = nofile;
        if (ncol < 1) {
            fprintf(stderr, "pr: No room for columns.\n");
            done();
        }
    }
    colw    = width / ncol;
    sncol   = ncol;
    sheader = header;
    plength = length - 5;
    if (ntflg)
        plength = length;
    if (--ncol < 0)
        ncol = 0;
    if (mflg)
        fp = NULL;
    if (fp) {
        if ((file = fopen(fp, "r")) == NULL) {
            if (tty == NULL)
                fprintf(stderr, "pr: can't open %s\n", fp);
            ncol   = sncol;
            header = sheader;
            return;
        }
        stat(fp, &sbuf);
        stamp = sbuf.st_mtime;
    } else {
        file = stdin;
        time(&stamp);
    }
    if (header == NULL)
        header = fp ? fp : "";
    cbuf     = ctime(&stamp);
    cbuf[16] = '\0';
    cbuf[24] = '\0';
    page     = 1;
    icol     = 0;
    colp[ncol] = bufp = 0;
    if (mflg == 0)
        nexbuf();
    while ((mflg && nofile) || (!mflg && tpgetc(ncol) > 0)) {
        if (mflg == 0) {
            // v7 wrapped backwards to &buffer[BUFS], one past the ring, and read it.
            if (--colp[ncol] < 0)
                colp[ncol] = BUFS - 1;
        }
        line = 0;
        if (ntflg == 0) {
            // v7 sprintf'd here with an unbounded -h argument (§6).
            snprintf(linebuf, sizeof(linebuf), "\n\n%s %s  %s Page %d\n\n\n", cbuf + 4,
                     cbuf + 20, header, page);
            for (cp = linebuf; *cp;)
                put(*cp++);
        }
        putpage();
        if (ntflg == 0)
            while (line < length)
                put('\n');
        page++;
    }
    fclose(file);
    ncol   = sncol;
    header = sheader;
}

//
// -m: one stream per file, side by side.  v7 stored into ifile[nofile] and only then tested
// nofile against the ceiling, and on a file it could not open it decremented nofile -- from
// zero, on the first argument, so isclosed[-1] was written.
//
static void mopen(char **ap)
{
    char **p, *p1;

    p = ap;
    while ((p1 = *p) != NULL && p++ <= lastarg) {
        if (nofile >= 10) {
            fprintf(stderr, "pr: Too many args\n");
            done();
        }
        if ((ifile[nofile] = fopen(p1, "r")) == NULL) {
            if (tty == NULL)
                fprintf(stderr, "pr: can't open %s\n", p1);
            continue;
        }
        isclosed[nofile] = 0;
        nofile++;
    }
}

static void putpage(void)
{
    int lastcol, i, c;
    int j;

    if (ncol == 0) {
        while (line < plength) {
            while ((c = tpgetc(0)) != 0 && c != '\n' && c != FF)
                putcp(c);
            putcp('\n');
            line++;
            if (c == FF)
                break;
        }
        return;
    }
    colp[0] = colp[ncol];
    if (mflg == 0)
        for (i = 1; i <= ncol; i++) {
            colp[i] = colp[i - 1];
            for (j = margin; j < length; j++)
                while ((c = tpgetc(i)) != '\n')
                    if (c == 0)
                        break;
        }
    while (line < plength) {
        lastcol = colw;
        for (i = 0; i < ncol; i++) {
            while ((c = pgetc(i)) != 0 && c != '\n')
                if (col < lastcol || tabc != 0)
                    put(c);
            if (c == 0)
                continue;
            if (tabc)
                put(tabc);
            else
                while (col < lastcol)
                    put(' ');
            lastcol += colw;
        }
        while ((c = pgetc(ncol)) != 0 && c != '\n')
            put(c);
        put('\n');
    }
}

//
// Refill the ring and re-plant the sentinel.  The read is bounded by the distance to the end
// of the ring so that it never wraps in the middle, which is what lets the sentinel be a
// single byte at a known position.
//
static void nexbuf(void)
{
    int n, i, d;

    n = BUFS - bufp;
    if (n > 512)
        n = 512;
    // The invariant v7 announced nowhere.  Every live cursor sits at or behind bufp, so its
    // forward distance is 0 (waiting on the sentinel) or large (behind).  A SMALL non-zero
    // distance means the writer has lapped it and the bytes it still owes are about to go.
    for (i = 0; i <= ncol; i++) {
        d = colp[i] - bufp;
        if (d < 0)
            d += BUFS;
        if (d > 0 && d <= n) {
            fprintf(stderr, "pr: too many columns for the buffer\n");
            done();
        }
    }
    if (feof(file) || (n = fread(&buffer[bufp], 1, n, file)) <= 0) {
        fclose(file);
        buffer[bufp] = ENDF;
        return;
    }
    bufp += n;
    if (bufp >= BUFS)
        bufp = 0;
    buffer[bufp] = REFILL;
}

static int tpgetc(int ai)
{
    int c, i;

    i = ai;
    if (mflg) {
        if ((c = getc(ifile[i])) == EOF) {
            if (isclosed[i] == 0) {
                isclosed[i] = 1;
                if (--nofile <= 0)
                    return 0;
            }
            return '\n';
        }
        if (c == FF && ncol > 0)
            c = '\n';
        return c;
    }
    for (;;) {
        c = buffer[colp[i]] & 0377;
        if (c == REFILL) {
            nexbuf();
            c = buffer[colp[i]] & 0377;
        }
        if (c == ENDF)
            return 0;
        if (++colp[i] >= BUFS)
            colp[i] = 0;
        if (c != 0)
            return c;
    }
}

static int pgetc(int i)
{
    int c;

    if (peekc) {
        c     = peekc;
        peekc = 0;
    } else
        c = tpgetc(i);
    if (tabc)
        return c;
    switch (c) {

    case '\t':
        icol++;
        if ((icol & 07) != 0)
            peekc = '\t';
        return ' ';

    case '\n':
        icol = 0;
        break;

    case 010:
    case 033:
        icol--;
        break;
    }
    if (c >= ' ')
        icol++;
    return c;
}

//
// Column tracking and blank compression.  A run of spaces becomes a tab when a tab stop can
// carry it, which is why every space goes through nspace rather than straight to the output.
//
static void put(int ac)
{
    int ns, c;

    c = ac;
    if (tabc) {
        putcp(c);
        if (c == '\n')
            line++;
        return;
    }
    switch (c) {

    case ' ':
        nspace++;
        col++;
        return;

    case '\n':
        col    = 0;
        nspace = 0;
        line++;
        break;

    case 010:
    case 033:
        if (--col < 0)
            col = 0;
        if (--nspace < 0)
            nspace = 0;
    }
    while (nspace) {
        if (nspace > 2 && col > (ns = ((col - nspace) | 07))) {
            nspace = col - ns - 1;
            putcp('\t');
        } else {
            nspace--;
            putcp(' ');
        }
    }
    if (c >= ' ')
        col++;
    putcp(c);
}

static void putcp(int c)
{
    if (page >= fpage)
        putchar(c);
}
