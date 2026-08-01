/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// col -- filter reverse line feeds.
//
//      col [ -bfx ]
//
// One of task C5b's seven (../TODO.md), and the one deliberate divergence in the task.
// README.md beside it is the account; §10 asks for a divergence to be written down twice and
// this header is one of the two, col.1 the other.
//
// THE DIVERGENCE: THE GREEK HALF-SHIFT IS GONE, AND WITH IT THE EIGHTH BIT.  v7's col tracks
// the Model 37 Teletype's shift-out/shift-in pair -- SO selects a Greek type box, SI selects
// back -- by stealing BIT 0200 OF EVERY STORED CHARACTER as the flag, and it masks its input
// with `c &= 0177' on the way in.  Both halves are fatal here.  This system's text is UTF-8
// end to end since task C11 (§11): a byte above 0177 is an ordinary part of a letter, the
// mask destroys it, `*p & ~GREEK' would destroy it again on the way out, and the printability
// test `c > 040 && c < 0177' then decides whether the wreckage is even kept.
//
// It is worth being exact about what a faithful col does here, because the answer is worse
// than losing the text would be.  `привет' is twelve bytes, `320 277 321 200 320 270 320 262
// 320 265 321 202'.  Masked with 0177 they become `P ? Q \0 P 8 P 2 P 5 Q \2', and the
// printability test drops the two that fell below a space -- so v7's col prints
//
//      P?QP8P2P5Q
//
// ten bytes of plausible-looking ASCII, two characters shorter than the six that went in.
// Not an error, not an empty line: a wrong answer that a reader could mistake for output.
//
// The alternative to deleting it was a parallel flag array beside lbuff, which is what ed's
// QESC prefix is the precedent for.  It was not worth it: there is no Model 37 on this
// machine, no way to attach one, and no producer of SO/SI here -- v7's col exists to
// post-process nroff, and ../TODO.md's exclusion table records that there is no nroff in
// this source tree at all.  So this is the cut getty(1) made to the speed table and stty(1)
// is told to make to its capability list: DELETE WHAT THIS HARDWARE HAS NOT GOT, rather than
// carry a mechanism that can only get in the way of something real.  What is left is a col
// that passes every byte through, which is the useful behaviour and the one §11 asks for.
//
// COLUMNS ARE COUNTED IN BYTES, which is the honest half of the same decision and is stated
// in col.1.  A two-byte Cyrillic letter occupies two columns, exactly as the shell's `?'
// matches one byte (§11).  Counting characters would mean decoding UTF-8 in outc(), and the
// half-line arithmetic this program exists for is about a TYPEWRITER's carriage: making it
// character-aware would be inventing a behaviour rather than porting one.
//
// setbuf(stdout, fbuff) HAD TO GO, AND IT TOOK A REAL BUG WITH IT.  `char fbuff[BUFSIZ]' is
// an automatic of main(), and BUFSIZ is 3072 here where it was 512 on a PDP-11 -- 512 words
// out of the four pages §6 gives the stack, an eighth of the ceiling nothing checks, for a
// buffer stdout would allocate from the heap anyway.  And main() RETURNS, after which exit()
// flushes stdout out of a frame that no longer exists.  Deleting the array and the setbuf
// call together fixes both.
//
// outc() HAD NO BOUND.  cp is advanced by every space, tab and printable character with
// nothing limiting it, and the cursor walks lbuff to reach it, so a long enough input line
// left the 800-byte buffer.  cp is clamped to MAXCOL now and the overstrike insert refuses to
// open two more bytes it has not got -- §6's rule, and col.1 says what is dropped.
//
// WHAT IS LEFT ALONE: page[] is a 256-entry sliding window of malloc'd half-lines, not a
// matrix, and it is the only allocation in task C5b.  The reverse and half line feeds, -b,
// -f and -x, and the tab-reconstruction in emit() are all v7's.
//
// NOT SETUID: it opens nothing at all -- it is stdin to stdout.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PL     256
#define ESC    '\033'
#define RLF    '\013'
#define LINELN 800
// The rightmost column outc() will place a character in.  Four bytes of slack: the character
// itself, the `\b' and the overstruck character an insert opens, and the terminator.
#define MAXCOL (LINELN - 4)

static char *page[PL];
static char lbuff[LINELN];
static int li; // where in lbuff the cursor sits; v7 kept a `char *line' here
static int bflag, xflag, fflag;
static int half;
static int cp, lp;
static int ll, llh, mustwr;
static int pcp = 0;
static const char *pgmname;

static void outc(int c);
static void store(int lno);
static void fetch(int lno);
static void emit(const char *s, int lineno);
static void incr(void);
static void decr(void);

// cp is the column the next character goes in.  Every advance goes through this, so that
// nothing in the program can walk the cursor past the end of lbuff.  See the header.
static void tocol(int n)
{
    cp = n > MAXCOL ? MAXCOL : n;
}

int main(int argc, char **argv)
{
    int i;
    int c;

    pgmname = argv[0];

    for (i = 1; i < argc; i++) {
        char *p;
        if (*argv[i] != '-') {
            fprintf(stderr, "%s: bad option %s\n", pgmname, argv[i]);
            exit(2);
        }
        for (p = argv[i] + 1; *p; p++) {
            switch (*p) {
            case 'b':
                bflag++;
                break;

            case 'x':
                xflag++;
                break;

            case 'f':
                fflag++;
                break;

            default:
                fprintf(stderr, "%s: bad option letter %c\n", pgmname, *p);
                exit(2);
            }
        }
    }

    for (ll = 0; ll < PL; ll++)
        page[ll] = 0;

    cp     = 0;
    ll     = 0;
    mustwr = PL;
    li     = 0;

    while ((c = getchar()) != EOF) {
        switch (c) {
        case '\n':
            incr();
            incr();
            cp = 0;
            continue;

        case '\0':
            continue;

        case ESC:
            c = getchar();
            switch (c) {
            case '7': // reverse full line feed
                decr();
                decr();
                break;

            case '8': // reverse half line feed
                if (fflag)
                    decr();
                else {
                    if (--half < -1) {
                        decr();
                        decr();
                        half += 2;
                    }
                }
                break;

            case '9': // forward half line feed
                if (fflag)
                    incr();
                else {
                    if (++half > 0) {
                        incr();
                        incr();
                        half -= 2;
                    }
                }
                break;
            }
            continue;

        case RLF:
            decr();
            decr();
            continue;

        case '\r':
            cp = 0;
            continue;

        case '\t':
            tocol((cp + 8) & -8);
            continue;

        case '\b':
            if (cp > 0)
                cp--;
            continue;

        case ' ':
            tocol(cp + 1);
            continue;

        default:
            // Printable, and a byte above 0177 is one: this system's text is UTF-8 and v7's
            // `c &= 0177' plus `c < 0177' deleted every letter that is not ASCII.  DEL is
            // still dropped, as it was.
            if (c > 040 && c != 0177) {
                outc(c);
                tocol(cp + 1);
            }
            continue;
        }
    }

    for (i = 0; i < PL; i++)
        if (page[(mustwr + i) % PL] != 0)
            emit(page[(mustwr + i) % PL], mustwr + i - PL);
    emit(" ", (llh + 1) & -2);
    return 0;
}

// Put c in column cp of the current half-line, overstriking with a backspace pair if
// something is there already and -b did not say to discard it.
static void outc(int c)
{
    if (lp > cp) {
        li = 0;
        lp = 0;
    }

    while (lp < cp && li < MAXCOL) {
        switch (lbuff[li]) {
        case '\0':
            lbuff[li] = ' ';
            lp++;
            break;

        case '\b':
            lp--;
            break;

        default:
            lp++;
        }
        li++;
    }
    while (li < MAXCOL && lbuff[li] == '\b')
        li += 2;
    if (bflag || lbuff[li] == '\0' || lbuff[li] == ' ')
        lbuff[li] = c;
    else {
        char c1, c2, c3;
        int j;

        // An overstrike opens two more bytes.  v7 opened them unconditionally and walked off
        // the end of lbuff on a long line; if the room is not there the character replaces
        // what was in the column instead, which is what -b asks for anyway.
        for (j = li; lbuff[j] != '\0'; j++)
            ;
        if (j + 2 >= LINELN) {
            lbuff[li] = c;
            return;
        }
        c1          = lbuff[++li];
        lbuff[li++] = '\b';
        c2          = lbuff[li];
        lbuff[li++] = c;
        while (c1) {
            c3          = lbuff[li];
            lbuff[li++] = c1;
            c1          = c2;
            c2          = c3;
        }
        lbuff[li] = '\0';
        lp        = 0;
        li        = 0;
    }
}

static void store(int lno)
{
    lno %= PL;
    if (page[lno] != 0)
        free(page[lno]);
    page[lno] = malloc((unsigned)strlen(lbuff) + 2);
    if (page[lno] == 0) {
        fprintf(stderr, "%s: no storage\n", pgmname);
        exit(2);
    }
    strcpy(page[lno], lbuff);
}

static void fetch(int lno)
{
    int i;

    lno %= PL;
    for (i = 0; lbuff[i] != '\0'; i++)
        lbuff[i] = '\0';
    li = 0;
    lp = 0;
    if (page[lno])
        strcpy(lbuff, page[lno]);
}

static void emit(const char *s, int lineno)
{
    static int cline = 0;
    int ncp;
    const char *p;

    if (*s) {
        while (cline < lineno - 1) {
            putchar('\n');
            pcp = 0;
            cline += 2;
        }
        if (cline != lineno) {
            putchar(ESC);
            putchar('9');
            cline++;
        }
        if (pcp)
            putchar('\r');
        pcp = 0;
        p   = s;
        while (*p) {
            ncp = pcp;
            while (*p++ == ' ') {
                if ((++ncp & 7) == 0 && !xflag) {
                    pcp = ncp;
                    putchar('\t');
                }
            }
            if (!*--p)
                break;
            while (pcp < ncp) {
                putchar(' ');
                pcp++;
            }
            putchar(*p);
            if (*p++ == '\b')
                pcp--;
            else
                pcp++;
        }
    }
}

static void incr(void)
{
    store(ll++);
    if (ll > llh)
        llh = ll;
    if (ll >= mustwr && page[ll % PL]) {
        emit(page[ll % PL], ll - PL);
        mustwr++;
        free(page[ll % PL]);
        page[ll % PL] = 0;
    }
    fetch(ll);
}

static void decr(void)
{
    if (ll > mustwr - PL) {
        store(ll--);
        fetch(ll);
    }
}
