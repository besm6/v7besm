/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// uniq -- report repeated lines in a file.
//
//      uniq [ -udc [ +n ] [ -n ] ] [ input [ output ] ]
//
// One of task C5b's seven (../README.md).  No §2 in it, no `long', no `%D'; what it has is one
// unbounded write, one ctype call that indexes off the end of a table, and one v7 property
// that is not a bug and is worth naming before somebody "fixes" it.
//
// gline() HAD NO BOUND.  It read `*buf++ = c' until a newline or end of file, into one of two
// 1000-byte buffers laid down next to each other, so a long enough line walked out of b1 and
// through b2 and past it.  §6 asks every port to bound the one fixed buffer it has, and this
// is uniq's.  The line is cut at LMAX now and what is left of it becomes the next line, which
// is comm(1)'s behaviour for the same situation and is what uniq.1.umm now says.
//
// AND ONE UPSTREAM BUG, THE SAME ONE rev HAD.  v7's gline() answers `end of file' the instant
// it sees EOF, whatever it has already read, so a final line with no newline was thrown away:
// `echo -n abc | uniq' printed nothing at all.  Worse than rev's, because the buffer is left
// unterminated on that path, so the discard is also what stops uniq printing whatever the
// previous line left behind it.  The line is kept now and the end of file is reported on the
// call after it.
//
// isdigit() ON AN ARBITRARY BYTE RUNS OFF THE TABLE.  <ctype.h>'s macros index
// `(_ctype_ + 1)[c]' and lib/libc/gen/ctype_.c is 129 entries -- it says so in its header,
// and adds that only isascii() may be applied to a byte above 0177.  v7 hands it argv[1][1],
// the character after a `-', which the caller chooses; `uniq -п' would have read past the
// end of the table and branched on whatever followed.  Guarded with isascii() (§11).
//
// THE BUFFERS STAY static, WHICH IS NOT A STYLE CHOICE.  b1 and b2 are 1000 bytes each, 334
// words together; as automatics they would take a twelfth of the four-page stack §6 measures
// and nothing checks that ceiling.  v7 wrote `static' and it is the right answer here for a
// reason v7 did not have.
//
// WHAT IS LEFT ALONE, all v7's and all in uniq.1.umm: -c's count field is four columns wide and a
// count past 9999 pushes the line right rather than being truncated; the comparison is over
// whole bytes, so `+n' skips n bytes and not n characters; and `-n' counts FIELDS, where a
// field is blank-delimited, so a Cyrillic word is one field like any other.
//
// NOT SETUID: it opens what the caller could open itself.
//
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#define LMAX 1000 // the longest line uniq will hold, terminator included

static int fields;
static int letters;
static int linec;
static char mode;
static int uniq;

static char b1[LMAX], b2[LMAX];

static int gline(char *buf);
static void pline(const char *buf);
static int equal(const char *s1, const char *s2);
static int skip(const char *s);

int main(int argc, char **argv)
{
    while (argc > 1) {
        if (*argv[1] == '-') {
            // isascii() first: a byte above 0177 is off the end of the 129-entry class
            // table, and the flag letter is the caller's to choose.  See the header.
            if (isascii(argv[1][1]) && isdigit(argv[1][1]))
                fields = atoi(&argv[1][1]);
            else
                mode = argv[1][1];
            argc--;
            argv++;
            continue;
        }
        if (*argv[1] == '+') {
            letters = atoi(&argv[1][1]);
            argc--;
            argv++;
            continue;
        }
        if (freopen(argv[1], "r", stdin) == NULL) {
            fprintf(stderr, "uniq: cannot open %s\n", argv[1]);
            exit(1);
        }
        break;
    }
    if (argc > 2 && freopen(argv[2], "w", stdout) == NULL) {
        fprintf(stderr, "uniq: cannot create %s\n", argv[2]);
        exit(1);
    }

    if (gline(b1))
        return 0;
    for (;;) {
        linec++;
        if (gline(b2)) {
            pline(b1);
            return 0;
        }
        if (!equal(b1, b2)) {
            pline(b1);
            linec = 0;
            do {
                linec++;
                if (gline(b1)) {
                    pline(b2);
                    return 0;
                }
            } while (equal(b1, b2));
            pline(b2);
            linec = 0;
        }
    }
}

// One line into buf, terminated and without its newline.  Nonzero at end of file.  A line
// longer than LMAX-1 is cut and its remainder comes back as the next line -- v7 had no bound
// here at all; see the header.
static int gline(char *buf)
{
    int c, n;

    n = 0;
    while ((c = getchar()) != '\n') {
        if (c == EOF) {
            buf[n] = 0;
            return n == 0; // a final line with no newline is still a line
        }
        if (n >= LMAX - 1)
            break;
        buf[n++] = c;
    }
    buf[n] = 0;
    return 0;
}

static void pline(const char *buf)
{
    switch (mode) {
    case 'u':
        if (uniq) {
            uniq = 0;
            return;
        }
        break;

    case 'd':
        if (uniq)
            break;
        return;

    case 'c':
        printf("%4d ", linec);
    }
    uniq = 0;
    fputs(buf, stdout);
    putchar('\n');
}

// The two lines, past whatever `-n' and `+n' say to ignore.  Sets `uniq' when they match,
// which is what -u and -d read.
static int equal(const char *s1, const char *s2)
{
    int i, j, c;

    i = skip(s1);
    j = skip(s2);
    while ((c = s1[i++]) != 0)
        if (c != s2[j++])
            return 0;
    if (s2[j] != 0)
        return 0;
    uniq++;
    return 1;
}

// Where the compared part of s begins: past `fields' blank-delimited fields, then past
// `letters' more bytes.
static int skip(const char *s)
{
    int i, nf, nl;

    i  = 0;
    nf = nl = 0;
    while (nf++ < fields) {
        while (s[i] == ' ' || s[i] == '\t')
            i++;
        while (!(s[i] == ' ' || s[i] == '\t' || s[i] == 0))
            i++;
    }
    while (nl++ < letters && s[i] != 0)
        i++;
    return i;
}
