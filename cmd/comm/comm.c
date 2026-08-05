/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// comm -- select or reject lines common to two sorted files.
//
//      comm [ - [ 123 ] ] file1 file2
//
// One of task C5b's seven (../TODO.md).  Nothing in it parses, so it has no §2 in it at all
// -- no `char *' relational anywhere -- and no `long', no `%D' and no character-indexed
// table.  What it does have is one construct that is undefined C everywhere and actively
// dangerous here, and one that stops being a lie on this machine.
//
// THE LEADING TABS WERE STRING LITERALS AND WERE WRITTEN INTO.  v7 assigns `""', `"\t"' and
// `"\t\t"' to ldr[0..2] and then, when -1 or -2 suppresses a column, stores a '\0' into the
// literal to shorten it -- ldr[1][0] and ldr[2][l--], with l counting down across the two
// flags.  A string literal lives in the CONST segment on this machine
// (cross/besm6/b.out.h carries a separate const size beside text and data), so the program
// was editing its own read-only image.  The port does not make the strings writable; it
// stops mutating them.  The leader is a function of `one' and `two' and is computed where it
// is printed, which is where it was always wanted, and `l' goes with it.
//
// Worth recording because it is the shape of the thing rather than this instance: v7's
// counter WAS correct.  `comm -12' and `comm -21' both end with column 3 unindented, because
// l is decremented by whichever flag comes first and read by whichever comes second.  It is
// a piece of cleverness that works and had no business existing, and the tempting reading --
// that two flags sharing a mutable counter must have an ordering bug -- is wrong.  What is
// wrong with it is only where it wrote.
//
// COMPARE() FORMED A POINTER BEFORE ITS BUFFER: `ra = --a' on a pointer to the first byte of
// lb1, then `*++ra' to undo it.  Never dereferenced out of range, so it worked, but it is
// C11 UB and it is this program's inner loop -- two out-of-line fat-pointer helpers per
// character where an index is a register test.  It is an index pair now.
//
// AND THE COLLATING ORDER IS NOT v7's, WITHOUT A LINE CHANGING.  comm requires sorted input
// and decides `sorted' with `*ra < *rb'.  `char' is SIGNED on a PDP-11 and UNSIGNED here
// (§3), so v7 collated a byte above 0177 BEFORE every ASCII character and this collates it
// after -- which is the byte order every other program on this image uses, and the order a
// UTF-8 line has to be sorted in for comm to agree with itself.  Nothing had to change for
// that; it is worth saying precisely because a diff cannot show it.  comm.1.umm says it too.
//
// WHAT IS LEFT ALONE, both v7's and both in comm.1.umm: a line longer than 255 bytes is cut and
// its remainder becomes the next line, and `-' names the standard input for either file.
//
// NOT SETUID: it opens what the caller could open itself.
//
#include <stdio.h>
#include <stdlib.h>

#define LB 256 // the longest line comm will hold, terminator included

static int one;
static int two;
static int three;

// The most a column is ever indented by.  wr() prints a suffix of this.
static const char tabs[] = "\t\t";

static FILE *ib1;
static FILE *ib2;

static int rd(FILE *file, char *buf);
static void wr(const char *str, int n);
static void copy(FILE *ibuf, char *lbuf, int n);
static int compare(const char *a, const char *b);
static FILE *openfil(const char *s);

int main(int argc, char **argv)
{
    char lb1[LB], lb2[LB];

    if (argc > 1) {
        if (*argv[1] == '-' && argv[1][1] != 0) {
            while (*++argv[1]) {
                switch (*argv[1]) {
                case '1':
                    one = 1;
                    break;
                case '2':
                    two = 1;
                    break;
                case '3':
                    three = 1;
                    break;
                default:
                    fprintf(stderr, "comm: illegal flag\n");
                    exit(1);
                }
            }
            argv++;
            argc--;
        }
    }

    if (argc < 3) {
        fprintf(stderr, "comm: arg count\n");
        exit(1);
    }

    ib1 = openfil(argv[1]);
    ib2 = openfil(argv[2]);

    if (rd(ib1, lb1) < 0) {
        if (rd(ib2, lb2) < 0)
            exit(0);
        copy(ib2, lb2, 2);
    }
    if (rd(ib2, lb2) < 0)
        copy(ib1, lb1, 1);

    for (;;) {
        switch (compare(lb1, lb2)) {
        case 0:
            wr(lb1, 3);
            if (rd(ib1, lb1) < 0) {
                if (rd(ib2, lb2) < 0)
                    exit(0);
                copy(ib2, lb2, 2);
            }
            if (rd(ib2, lb2) < 0)
                copy(ib1, lb1, 1);
            continue;

        case 1:
            wr(lb1, 1);
            if (rd(ib1, lb1) < 0)
                copy(ib2, lb2, 2);
            continue;

        case 2:
            wr(lb2, 2);
            if (rd(ib2, lb2) < 0)
                copy(ib1, lb1, 1);
            continue;
        }
    }
}

// One line into buf, terminated and without its newline.  -1 at end of file.  A line that
// does not fit is cut, and what is left of it comes back as the next line -- v7's, and
// comm.1.umm says so.
static int rd(FILE *file, char *buf)
{
    int i, c;

    i = 0;
    while ((c = getc(file)) != EOF) {
        *buf = c;
        if (c == '\n' || i > LB - 2) {
            *buf = '\0';
            return 0;
        }
        i++;
        buf++;
    }
    return -1;
}

// Column n, indented past the columns before it that are still being printed.  v7 shortened
// three string constants in place to get this; it is arithmetic here.  See the header.
static void wr(const char *str, int n)
{
    int ntabs;

    switch (n) {
    case 1:
        if (one)
            return;
        break;
    case 2:
        if (two)
            return;
        break;
    case 3:
        if (three)
            return;
    }

    ntabs = n - 1;
    if (n > 1 && one)
        ntabs--;
    if (n > 2 && two)
        ntabs--;
    printf("%s%s\n", tabs + (2 - ntabs), str);
}

static void copy(FILE *ibuf, char *lbuf, int n)
{
    do {
        wr(lbuf, n);
    } while (rd(ibuf, lbuf) >= 0);

    exit(0);
}

// 0 when the two lines are the same, 1 when a sorts first, 2 when b does.  `char' is
// unsigned here, so a byte above 0177 collates AFTER the ASCII rather than before it as it
// did on a PDP-11 -- see the header.
static int compare(const char *a, const char *b)
{
    int i;

    for (i = 0; a[i] == b[i]; i++)
        if (a[i] == '\0')
            return 0;
    if (a[i] < b[i])
        return 1;
    return 2;
}

static FILE *openfil(const char *s)
{
    FILE *b;

    if (s[0] == '-' && s[1] == 0)
        b = stdin;
    else if ((b = fopen(s, "r")) == NULL) {
        fprintf(stderr, "comm: cannot open %s\n", s);
        exit(1);
    }
    return b;
}
