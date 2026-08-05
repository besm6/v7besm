/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// split -- break a file into pieces of n lines each.
//
//      split [ -n ] [ file [ name ] ]
//
// One of task C5a's six (../README.md).  Nothing here is §2, §3 or §11: it is pure stdio,
// getc to putc, no long, no character table, and the copy is byte-transparent, so a UTF-8
// file comes out of it in pieces that are still UTF-8.
//
// WHAT IS HERE INSTEAD IS THREE UPSTREAM BUGS, and ../README.md's rule is that those are
// fixed rather than carried, with the fix saying which it is.
//
// 1.  `split -0' DID NOT TERMINATE.  count is the lines per piece and v7 took it straight
//     from atoi() with no test, so a count of zero made the `for(i=0; i<count; i++)' a
//     zero-trip loop: nothing was read, nothing was opened, os stayed NULL, fclose(NULL) was
//     called, and `goto loop' went round again -- forever, consuming no input and producing
//     no output.  A count below one is refused now.  It is also the reason this program
//     could not have had a b6sim case at all in the shape v7 left it: ../README.md §9 says a
//     program that does not terminate cannot be one.
//
// 2.  fname[100] WAS UNBOUNDED AGAINST THE PREFIX.  The name is built by copying `ofil' in
//     and appending two letters, with no length test anywhere, so a prefix of a hundred
//     bytes wrote past the array.  The prefix is measured once, at startup, before anything
//     is created.
//
// 3.  THE SUFFIX WRAPPED OUT OF THE ALPHABET.  It is `fnumber/26 + 'a'' and `fnumber%26 +
//     'a'', which is aa..zz for the first 676 pieces and then `{a', `{b', ... with no limit
//     and no complaint -- so split.1.umm's promise that the names come out in lexicographic
//     order stopped being true exactly when a user had enough pieces to care.  The 677th
//     piece is refused.  What is already written stays written, which is the only useful
//     thing to do: the input has been consumed.
//
// And one piece of sloppiness rather than a bug: the option switch had no `default', so
// `split -x' was silently ignored and the file was split into thousand-line pieces the user
// had not asked for.  It is a diagnostic now.
//
// THE PIECES LAND IN THE WORKING DIRECTORY, `xaa' onwards, because the prefix defaults to
// `x' and is passed to fopen() with no directory part -- so a prefix containing a `/' writes
// wherever it points.  That is v7's and split.1.umm now says it.  An existing piece is
// truncated without warning, the mode being "w"; also v7's, also now documented.
//
// NOT SETUID: it creates what the caller could create itself.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NAMESIZE 100       // v7's fname[100]: the prefix, two suffix letters and a NUL
#define NPIECES  (26 * 26) // what a two-letter suffix can name

static int count = 1000;
static int fnumber;
static char fname[NAMESIZE];
static char *ifil;
static char *ofil;
static FILE *is;
static FILE *os;

// The next output name: the prefix and a two-letter suffix, aa, ab, ... zz.  The prefix has
// already been measured against NAMESIZE in main(), so the copy is bounded.
static void newname(void)
{
    int f;

    if (fnumber >= NPIECES) {
        fprintf(stderr, "split: more than %d output files\n", NPIECES);
        exit(1);
    }
    for (f = 0; ofil[f]; f++)
        fname[f] = ofil[f];
    fname[f++] = fnumber / 26 + 'a';
    fname[f++] = fnumber % 26 + 'a';
    fname[f]   = '\0';
    fnumber++;
}

int main(int argc, char *argv[])
{
    int i, c, f;
    int iflg = 0;

    for (i = 1; i < argc; i++)
        if (argv[i][0] == '-')
            switch (argv[i][1]) {
            case '\0':
                iflg = 1;
                continue;

            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                count = atoi(argv[i] + 1);
                continue;

            default:
                fprintf(stderr, "usage: split [ -n ] [ file [ name ] ]\n");
                exit(1);
            }
        else if (iflg)
            ofil = argv[i];
        else {
            ifil = argv[i];
            iflg = 2;
        }
    if (count < 1) {
        // v7 spun here forever rather than saying this.
        fprintf(stderr, "split: line count must be positive\n");
        exit(1);
    }
    if (iflg != 2)
        is = stdin;
    else if ((is = fopen(ifil, "r")) == NULL) {
        fprintf(stderr, "cannot open input\n");
        exit(1);
    }
    if (ofil == 0)
        ofil = "x";
    if ((int)strlen(ofil) + 3 > NAMESIZE) {
        fprintf(stderr, "split: output name too long\n");
        exit(1);
    }

loop:
    f = 1;
    for (i = 0; i < count; i++)
        do {
            c = getc(is);
            if (c == EOF) {
                if (f == 0)
                    fclose(os);
                exit(0);
            }
            if (f) {
                newname();
                if ((os = fopen(fname, "w")) == NULL) {
                    fprintf(stderr, "Cannot create output\n");
                    exit(1);
                }
                f = 0;
            }
            putc(c, os);
        } while (c != '\n');
    fclose(os);
    goto loop;
}
