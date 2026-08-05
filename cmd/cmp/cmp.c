/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// cmp -- compare two files.
//
//      cmp [ -l ] [ -s ] file1 file2 [ skip1 [ skip2 ] ]
//
// One of task C5a's six (../README.md).  Byte at a time through getc(), no buffer, no
// pointer walk: nothing here is §2's business.
//
// THE SEVEN longs, which is what ../README.md §3's table credited this file with and is
// exactly right -- `line', `chr', `skip1', `skip2', otoi()'s forward declaration, its
// definition and its accumulator.  A long is an int is one 41-bit word here, so all seven
// are int and the three `%ld' conversions are `%d'.  The counts they hold are a byte offset
// and a line number in a file that cannot exceed the disk, and 41 bits is 1.1e12.
//
// otoi() COULD INDEX ctype OUT OF RANGE, which is §11 arriving in the one place nobody
// looks: the skip arguments.  v7 wrote `while(isdigit(*s))', and *s is a char -- unsigned
// here -- so a skip argument beginning with a UTF-8 byte hands isdigit() a subscript of
// 128..255 over a 129-entry _ctype_ table (lib/libc/gen/ctype_.c says so in those words) and
// reads whatever follows it.  The loop tests the digit range directly now.
//
// WHICH ALSO FIXES A BUG THAT HAS NOTHING TO DO WITH THIS MACHINE.  v7 chose base 8 on a
// leading `0' and then accepted every digit isdigit() did, so `cmp a b 09' skipped nine
// bytes and `cmp a b 08' skipped eight -- an octal parse quietly accepting digits octal does
// not have.  The digit is checked against the base now.  cmp.1.umm documents the two skip
// operands for the first time; v7's page never mentioned them.
//
// DIAGNOSTICS GO TO STANDARD OUTPUT, which is v7's and is left alone.  It is visible in the
// test cases -- run-prog-test.sh merges the two streams anyway -- and changing it would make
// this the one program of the six that does not report the way its manual page's ancestors
// did.  What -s suppresses is the `cannot open' line as well as the difference report, which
// is also v7's.
//
// NOT SETUID: it opens what the caller could open itself.
//
#include <stdio.h>
#include <stdlib.h>

static FILE *file1, *file2;
static int eflg;
static int lflg = 1;
static int line = 1;
static int chr  = 0;
static int skip1;
static int skip2;

// A count of bytes to skip: octal if it begins with `0', decimal otherwise.
static int otoi(const char *s)
{
    int v;
    int base;

    v    = 0;
    base = 10;
    if (*s == '0')
        base = 8;
    while (*s >= '0' && *s < '0' + base)
        v = v * base + *s++ - '0';
    return v;
}

int main(int argc, char **argv)
{
    int c1, c2;
    char *arg;

    if (argc < 3)
        goto narg;
    arg = argv[1];
    if (arg[0] == '-' && arg[1] == 's') {
        lflg--;
        argv++;
        argc--;
    }
    arg = argv[1];
    if (arg[0] == '-' && arg[1] == 'l') {
        lflg++;
        argv++;
        argc--;
    }
    if (argc < 3)
        goto narg;
    arg = argv[1];
    if (arg[0] == '-' && arg[1] == 0)
        file1 = stdin;
    else if ((file1 = fopen(arg, "r")) == NULL)
        goto barg;
    arg = argv[2];
    if ((file2 = fopen(arg, "r")) == NULL)
        goto barg;
    if (argc > 3)
        skip1 = otoi(argv[3]);
    if (argc > 4)
        skip2 = otoi(argv[4]);
    while (skip1) {
        if ((c1 = getc(file1)) == EOF) {
            arg = argv[1];
            goto earg;
        }
        skip1--;
    }
    while (skip2) {
        if ((c2 = getc(file2)) == EOF) {
            arg = argv[2];
            goto earg;
        }
        skip2--;
    }

loop:
    chr++;
    c1 = getc(file1);
    c2 = getc(file2);
    if (c1 == c2) {
        if (c1 == '\n')
            line++;
        if (c1 == EOF) {
            if (eflg)
                exit(1);
            exit(0);
        }
        goto loop;
    }
    if (lflg == 0)
        exit(1);
    if (c1 == EOF) {
        arg = argv[1];
        goto earg;
    }
    if (c2 == EOF)
        goto earg;
    if (lflg == 1) {
        printf("%s %s differ: char %d, line %d\n", argv[1], arg, chr, line);
        exit(1);
    }
    eflg = 1;
    printf("%6d %3o %3o\n", chr, c1, c2);
    goto loop;

narg:
    printf("cmp: arg count\n");
    exit(2);

barg:
    if (lflg)
        printf("cmp: cannot open %s\n", arg);
    exit(2);

earg:
    printf("cmp: EOF on %s\n", arg);
    exit(1);
}
