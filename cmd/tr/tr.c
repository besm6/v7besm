/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// tr -- transliterate, squeeze or delete characters.
//
//      tr [ -cds ] [ string1 [ string2 ] ]
//
// One of task C5b's seven (../TODO.md), and the cheapest of them: v7's tr is EIGHT-BIT CLEAN
// ALREADY and is the only program in the set that is.  Its three tables are `[256]' as v7
// wrote them (§11 asks for exactly that and finds it), and seven of the eight `&0377' masks
// scattered over them are therefore no-ops rather than the bugs the same expression is
// elsewhere -- 0377 is 255 and the tables have 256 slots.  Those seven are deleted, `char'
// being unsigned here (§3) so that a byte is already a value in 0..255.
//
// THE EIGHTH MASK IS LOAD-BEARING AND STAYS, which is the reason to count them rather than
// delete them all.  nextc()'s `return(c&0377)' is not applied to a byte: `\ooo' accepts up
// to three octal digits, so `\777' arrives as 511, and without the mask that indexes
// code[511] in a 256-entry table.  On a PDP-11 it was one more piece of belt and braces
// among seven; here it is the only one of the eight that does anything.
//
// THE SETS ARE BYTES, and that is worth stating rather than leaving to be found.  A
// multi-byte character can only be named in string1 or string2 by its bytes, so `tr п н'
// does not do what it looks like it does.  What byte transparency buys instead is the case
// that matters far more often: `tr a-z A-Z' passes every Cyrillic letter through UNTOUCHED,
// because code[i] defaults to i for all 256 values.  A tr that masked to 0177 -- which is
// what most of this task's other six did -- would have mangled them.  tr.1.umm says so.
//
// ONE WRITE INTO A STRING LITERAL, REMOVED.  v7's nextc() ends with `if(c==0) *--s->p = 0;',
// which backs the cursor onto the terminator and stores a 0 over the 0 it just read.  The
// store never changes a byte -- c is 0 precisely because that byte was -- but with no
// argument at all the cursor points into the `""' this file assigns at startup, and a string
// literal lives in the CONST segment here (cross/besm6/b.out.h carries a separate const
// size).  The decrement alone is exactly equivalent and touches nothing.
//
// TWO v7 SEMANTICS RECORDED RATHER THAN FIXED, both visible and both in tr.1.umm: next() returns
// 0 to mean end-of-set, so NUL can never be a member of either set; and the copy loop drops
// a NUL from the input stream instead of passing it on.
//
// NOT SETUID: it opens nothing at all -- it is stdin to stdout.
//
#include <stdio.h>
#include <stdlib.h>

#define NCHARS 256 // a byte indexes these, so 256 and not 128 (§11)

static int dflag;
static int sflag;
static int cflag;
static int save;

static char code[NCHARS];
static char squeez[NCHARS];
static char vect[NCHARS];

// A cursor over one of the two argument sets.  `last' is the character most recently
// produced, and `max' the far end of a `a-z' range still being walked out.
struct set {
    int last, max;
    char *p;
};

static struct set set1, set2;

static int next(struct set *s);
static int nextc(struct set *s);

int main(int argc, char **argv)
{
    int i, j, c, d, lastd;
    char *comp;

    set1.last = set2.last = 0;
    set1.max = set2.max = 0;
    set1.p = set2.p = "";

    if (--argc > 0) {
        argv++;
        if (*argv[0] == '-' && argv[0][1] != 0) {
            while (*++argv[0])
                switch (*argv[0]) {
                case 'c':
                    cflag++;
                    continue;
                case 'd':
                    dflag++;
                    continue;
                case 's':
                    sflag++;
                    continue;
                }
            argc--;
            argv++;
        }
    }
    if (argc > 0)
        set1.p = argv[0];
    if (argc > 1)
        set2.p = argv[1];

    for (i = 0; i < NCHARS; i++)
        code[i] = vect[i] = 0;
    comp = vect;
    if (cflag) {
        while ((c = next(&set1)) != 0)
            vect[c] = 1;
        // The complement, laid down over the flags it was computed from -- j never overtakes
        // i, so the rewrite is safe.  It starts at 1 because 0 is the end-of-set marker.
        j = 0;
        for (i = 1; i < NCHARS; i++)
            if (vect[i] == 0)
                vect[j++] = i;
        vect[j] = 0;
    }
    for (i = 0; i < NCHARS; i++)
        squeez[i] = 0;
    lastd = 0;
    for (;;) {
        if (cflag)
            c = *comp++;
        else
            c = next(&set1);
        if (c == 0)
            break;
        d = next(&set2);
        if (d == 0)
            d = lastd;
        else
            lastd = d;
        squeez[d] = 1;
        code[c]   = dflag ? 1 : d;
    }
    while ((d = next(&set2)) != 0)
        squeez[d] = 1;
    squeez[0] = 1;
    for (i = 0; i < NCHARS; i++) {
        if (code[i] == 0)
            code[i] = i;
        else if (dflag)
            code[i] = 0;
    }

    while ((c = getc(stdin)) != EOF) {
        if (c == 0)
            continue; // v7: a NUL never reaches the output
        if ((c = code[c]) != 0)
            if (!sflag || c != save || !squeez[c])
                putchar(save = c);
    }
    return 0;
}

// The next character of a set, walking out `a-z' ranges as it goes.  0 means the set is
// exhausted, which is why a NUL cannot be a member of one.
static int next(struct set *s)
{
again:
    if (s->max) {
        if (s->last++ < s->max)
            return s->last;
        s->max = s->last = 0;
    }
    if (s->last && *s->p == '-') {
        nextc(s);
        s->max = nextc(s);
        if (s->max == 0) {
            s->p--;
            return '-';
        }
        if (s->max < s->last) {
            s->last = s->max - 1;
            return '-';
        }
        goto again;
    }
    return s->last = nextc(s);
}

// One character of a set, with v7's `\ooo' and `\c' escapes.  At the terminator the cursor
// backs up so that every later call answers 0 too -- v7 stored a 0 there as well, which is
// a write into a string literal when the set is the empty default.  See the header.
static int nextc(struct set *s)
{
    int c, i, n;

    c = *s->p++;
    if (c == '\\') {
        i = n = 0;
        while (i < 3 && (c = *s->p) >= '0' && c <= '7') {
            n = n * 8 + c - '0';
            i++;
            s->p++;
        }
        if (i > 0)
            c = n;
        else
            c = *s->p++;
    }
    if (c == 0)
        s->p--;
    return c & 0377;
}
