/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// look -- find lines in a sorted list.
//
//      look [ -dft ] string [ file ]
//
// One of task C5b's seven (../TODO.md).  No §2 in it; six `long's that are one word here
// (§3); and four things that had to change, of which the last is the one that decides how
// this program can be tested at all.
//
// puts(entry, stdout) DOES NOT COMPILE, AND MUST NOT BECOME fputs.  v7 calls puts with two
// arguments; <stdio.h> declares `int puts(const char *)'.  This machine passes r14 = negative
// argument count (doc/Besm6_Calling_Conventions.md), so the extra argument is not the
// harmless surplus it was on a PDP-11.  The fix has to keep the NEWLINE v7's puts appended --
// plain fputs(entry, stdout) drops it and runs the whole answer together on one line.
//
// THREE UNBOUNDED WRITES, and the first is reachable from the command line.  canon(argv[1],
// key) copied an argument of any length into key[50]; getword() filled entry[250] with no
// bound at all, so a dictionary line over 249 bytes ran past it; and canon(entry, word)
// inherited that.  All three are bounded now, which is §6's rule and the thing every port in
// this tree has had to do.
//
// isalnum() AND isupper() ON A UTF-8 BYTE, and here the question is not merely safety.
// <ctype.h> indexes a 129-entry table (lib/libc/gen/ctype_.c), so a byte above 0177 reads off
// the end of it -- but bounding the call is not enough, because the two options that use it
// have to MEAN something for a Cyrillic dictionary.  `-d' compares only letters and digits,
// and `-f' folds case.  A byte above 0177 is part of a multi-byte letter: it is a word
// constituent, so -d keeps it, and it has no case to fold, so -f leaves it alone.  That is
// the rule this port takes, it makes `look -df' on a Cyrillic dictionary do the obvious
// thing, and look.1.umm states it (§11).
//
// THE DEFAULT DICTIONARY IS ON THE IMAGE, and putting it there is what makes the bare
// `look word' form mean anything.  /usr/dict/words here is a small sorted list -- see
// words in this directory, and ../../root.manifest -- and not v7's 25,000-entry one, which
// is not in this tree.  Two consequences, and the second is ../README.md §9's rule in its
// purest form:
//
//   * The list is short, so `look' is a demonstration and a test subject rather than a
//     spelling authority.  look.1.umm says which.
//   * NO b6sim CASE MAY USE THE DEFAULT PATH.  Under b6sim the system calls are the HOST's,
//     so /usr/dict/words is the build machine's file -- on macOS it does not exist at all,
//     the list living at /usr/share/dict/words -- and a case that read it would assert
//     something about whoever ran the build.  Every case in test/ names its dictionary
//     explicitly; the default path is asserted under the booted kernel, in
//     kernel/test/filters, and nowhere else.
//
// NOT SETUID: it opens what the caller could open itself.
//
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#define ENTRYSZ 250 // a dictionary line, terminator included
#define KEYSZ   250 // the canonicalised search key -- v7 gave this 50 and did not bound it

static FILE *dfile;
static const char *filenam = "/usr/dict/words";

static int fold;
static int dict;
static int tab;
static char entry[ENTRYSZ];
static char word[KEYSZ];
static char key[KEYSZ];

static int compare(const char *s, const char *t);
static int getword(char *w);
static void canon(const char *old, char *new, int size);

// A byte above 0177 is part of a multi-byte character: a word constituent with no case.
// Both tests keep it away from the 129-entry class table besides.  See the header.
static int wordbyte(int c)
{
    return c > 0177 || isalnum(c);
}

static int upperbyte(int c)
{
    return c <= 0177 && isupper(c);
}

int main(int argc, char **argv)
{
    int c;
    int top, bot, mid;

    while (argc >= 2 && *argv[1] == '-') {
        for (;;) {
            switch (*++argv[1]) {
            case 'd':
                dict++;
                continue;
            case 'f':
                fold++;
                continue;
            case 't':
                tab = argv[1][1];
                if (tab)
                    ++argv[1];
                continue;
            case 0:
                break;
            default:
                continue;
            }
            break;
        }
        argc--;
        argv++;
    }
    if (argc <= 1)
        return 0;
    if (argc == 2) {
        fold++;
        dict++;
    } else
        filenam = argv[2];
    dfile = fopen(filenam, "r");
    if (dfile == NULL) {
        fprintf(stderr, "look: can't open %s\n", filenam);
        exit(2);
    }
    canon(argv[1], key, KEYSZ);
    bot = 0;
    fseek(dfile, 0, 2);
    top = ftell(dfile);
    for (;;) {
        mid = (top + bot) / 2;
        fseek(dfile, mid, 0);
        do {
            c = getc(dfile);
            mid++;
        } while (c != EOF && c != '\n');
        if (!getword(entry))
            break;
        canon(entry, word, KEYSZ);
        switch (compare(key, word)) {
        case -2:
        case -1:
        case 0:
            if (top <= mid)
                break;
            top = mid;
            continue;
        case 1:
        case 2:
            bot = mid;
            continue;
        }
        break;
    }
    fseek(dfile, bot, 0);
    while (ftell(dfile) < top) {
        if (!getword(entry))
            return 0;
        canon(entry, word, KEYSZ);
        switch (compare(key, word)) {
        case -2:
            return 0;
        case -1:
        case 0:
            puts(entry);
            break;
        case 1:
        case 2:
            continue;
        }
        break;
    }
    while (getword(entry)) {
        canon(entry, word, KEYSZ);
        switch (compare(key, word)) {
        case -1:
        case 0:
            puts(entry);
            continue;
        }
        break;
    }
    return 0;
}

// 0 when equal, -1 when s is a prefix of t, 1 when t is a prefix of s, -2/2 otherwise.
static int compare(const char *s, const char *t)
{
    int i;

    for (i = 0; s[i] == t[i]; i++)
        if (s[i] == 0)
            return 0;
    return s[i] == 0 ? -1 : t[i] == 0 ? 1 : s[i] < t[i] ? -2 : 2;
}

// One line of the dictionary into w, terminated and without its newline.  0 at end of file.
// v7 had no bound here; a long line ran past the array.
static int getword(char *w)
{
    int c, n;

    n = 0;
    for (;;) {
        c = getc(dfile);
        if (c == EOF)
            return 0;
        if (c == '\n')
            break;
        if (n < ENTRYSZ - 1)
            w[n++] = c;
    }
    w[n] = 0;
    return 1;
}

// The comparable form of a line: up to the tab character if one was named, letters and digits
// only under -d, folded under -f.  `size' is the room in `new', which v7 did not have.
static void canon(const char *old, char *new, int size)
{
    int c, n;

    n = 0;
    for (;;) {
        c = *old++;
        if (c == 0 || c == tab)
            break;
        if (dict && !wordbyte(c))
            continue;
        if (n >= size - 1)
            break;
        if (fold && upperbyte(c))
            c += 'a' - 'A';
        new[n++] = c;
    }
    new[n] = 0;
}
