/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// join -- relational database operator: combine two sorted files on a common field.
//
//      join [ -an ] [ -e s ] [ -jn m ] [ -o list ] [ -tc ] file1 file2
//
// One of task C5f's seven (../README.md).  No §2 -- not a pointer relational in the file -- no
// `%D', and the two `long's are file offsets and are one word here.  What it had was three
// unbounded reads and one that could not be bounded at all.
//
// NFLD WAS ENFORCED NOWHERE.  input() wrote `*pp++ = bp' inside `for (i = 0; ; i++)' and
// then `*pp = 0', into `char *ppi[2][20]' -- so a line of more than twenty fields ran off
// file 1's row into file 2's, and past the end of the array into s1 and s2 beside it.  §6's
// recurring finding, and the sixth port in a row to have one.  It is a diagnostic now, and
// the array carries one slot for the terminator so that the bound and the array agree.
//
// A LINE WITH NO FINAL NEWLINE READ PAST ITSELF.  v7 says so in a comment -- "fails badly if
// string doesn't have \n at end" -- and it is exact: the field loop ends on the '\0', writes
// a '\0' over it, steps past it, and reads whatever the rest of the 3072-byte buffer holds.
// The loop stops at the terminator now.
//
// A JOIN FIELD PAST THE END OF A LINE WAS A NULL DEREFERENCE.  `-j1 5' over a line with two
// fields fetched ppi[F1][4], which input() had left as the terminator, and handed NULL to
// strcmp() and to printf("%s").  A short line's missing field is the empty string here, in
// the comparison and in the output alike.
//
// AND FILE 2 CANNOT BE STANDARD INPUT.  The many-to-many join rewinds it with ftell()/fseek()
// once per group of equal keys, so `-' is accepted for file 1 and only for file 1.  v7 said
// so nowhere and simply produced a wrong answer on the first repeated key.  It is a
// diagnostic now, and join.1.umm says it.
//
// error() WAS VARARGS BY FIXED PARAMETERS -- `error(s1,s2,s3,s4,s5)' with only s1 typed,
// handed straight to fprintf.  Every call site passes at most one string, so it takes one.
//
// BUFSIZ IS 3072 HERE and was 512 on a PDP-11, so the two line buffers are six times the
// size they were and a line may be six times as long.  `char buf[2][BUFSIZ]' is 1,024 words,
// the largest object in the program; fgets() takes the macro, so nothing else had to change.
//
// NOT SETUID: it opens what the caller could open itself.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define F1   0
#define F2   1
#define NFLD 20 // max fields per line

static FILE *f[2];
static char buf[2][BUFSIZ];    // input lines
static char *ppi[2][NFLD + 1]; // pointers to fields in lines, and a terminator
static int nf[2];              // how many of them this line actually has
static int j1 = 1;             // join of this field of file 1
static int j2 = 1;             // join of this field of file 2
static int olist[2 * NFLD];    // output these fields
static int olistf[2 * NFLD];   // from these files
static int no;                 // number of entries in olist
static int sep1 = ' ';         // default field separator
static int sep2 = '\t';
static const char *nullstr = "";
static int aflg;

static int input(int n);
static const char *fld(int n, int k);
static void output(int on1, int on2);
static _Noreturn void error(const char *fmt, const char *arg);

#define comp() strcmp(fld(F1, j1), fld(F2, j2))

int main(int argc, char **argv)
{
    int i;
    int n1, n2;
    long top2, bot2;

    while (argc > 1 && argv[1][0] == '-') {
        if (argv[1][1] == '\0')
            break;
        switch (argv[1][1]) {
        case 'a':
            switch (argv[1][2]) {
            case '1':
                aflg |= 1;
                break;
            case '2':
                aflg |= 2;
                break;
            default:
                aflg |= 3;
            }
            break;
        case 'e':
            if (argc < 3)
                error("-e wants an argument", "");
            nullstr = argv[2];
            argv++;
            argc--;
            break;
        case 't':
            sep1 = sep2 = argv[1][2];
            break;
        case 'o':
            for (no = 0; no < 2 * NFLD; no++) {
                if (argc < 3)
                    break;
                if (argv[2][0] == '1' && argv[2][1] == '.') {
                    olistf[no] = F1;
                    olist[no]  = atoi(&argv[2][2]);
                } else if (argv[2][0] == '2' && argv[2][1] == '.') {
                    olist[no]  = atoi(&argv[2][2]);
                    olistf[no] = F2;
                } else
                    break;
                argc--;
                argv++;
            }
            break;
        case 'j':
            if (argc < 3)
                error("-j wants an argument", "");
            if (argv[1][2] == '1')
                j1 = atoi(argv[2]);
            else if (argv[1][2] == '2')
                j2 = atoi(argv[2]);
            else
                j1 = j2 = atoi(argv[2]);
            argc--;
            argv++;
            break;
        }
        argc--;
        argv++;
    }
    for (i = 0; i < no; i++)
        olist[i]--; // 0 origin
    if (argc != 3)
        error("usage: join [-j1 x -j2 y] [-o list] file1 file2", "");
    j1--;
    j2--; // everyone else believes in 0 origin
    if (j1 < 0 || j1 >= NFLD || j2 < 0 || j2 >= NFLD)
        error("join field out of range", "");
    if (argv[1][0] == '-' && argv[1][1] == '\0')
        f[F1] = stdin;
    else if ((f[F1] = fopen(argv[1], "r")) == NULL)
        error("can't open %s", argv[1]);
    // File 2 is rewound with fseek() once per group of equal keys, so it has to be seekable.
    // v7 accepted `-' here and then quietly produced a wrong answer.
    if (argv[2][0] == '-' && argv[2][1] == '\0')
        error("file2 cannot be standard input", "");
    if ((f[F2] = fopen(argv[2], "r")) == NULL)
        error("can't open %s", argv[2]);

#define get1() n1 = input(F1)
#define get2() n2 = input(F2)
    get1();
    bot2 = ftell(f[F2]);
    top2 = bot2;
    get2();
    while ((n1 > 0 && n2 > 0) || (aflg != 0 && n1 + n2 > 0)) {
        if ((n1 > 0 && n2 > 0 && comp() > 0) || n1 == 0) {
            if (aflg & 2)
                output(0, n2);
            bot2 = ftell(f[F2]);
            get2();
        } else if ((n1 > 0 && n2 > 0 && comp() < 0) || n2 == 0) {
            if (aflg & 1)
                output(n1, 0);
            get1();
        } else /*(n1>0 && n2>0 && comp()==0)*/ {
            while (n2 > 0 && comp() == 0) {
                output(n1, n2);
                top2 = ftell(f[F2]);
                get2();
            }
            fseek(f[F2], bot2, 0);
            get2();
            get1();
            for (;;) {
                if (n1 > 0 && n2 > 0 && comp() == 0) {
                    output(n1, n2);
                    get2();
                } else if ((n1 > 0 && n2 > 0 && comp() < 0) || n2 == 0) {
                    fseek(f[F2], bot2, 0);
                    get2();
                    get1();
                } else /*(n1>0 && n2>0 && comp()>0 || n1==0)*/ {
                    fseek(f[F2], top2, 0);
                    bot2 = top2;
                    get2();
                    break;
                }
            }
        }
    }
    return 0;
}

//
// Get an input line and split it into fields.  v7 had no bound on the field count at all,
// and walked past the end of a line that had no final newline.
//
static int input(int n)
{
    int i, c;
    char *bp;
    char **pp;

    bp = buf[n];
    pp = ppi[n];
    if (fgets(bp, BUFSIZ, f[n]) == NULL) {
        pp[0] = NULL;
        nf[n] = 0;
        return 0;
    }
    for (i = 0;;) {
        if (sep1 == ' ') // strip multiples
            while ((c = *bp) == sep1 || c == sep2)
                bp++; // skip blanks
        else
            c = *bp;
        if (c == '\n' || c == '\0')
            break;
        if (i >= NFLD)
            error("too many fields on one line", "");
        pp[i++] = bp; // record beginning
        while ((c = *bp) != sep1 && c != '\n' && c != sep2 && c != '\0')
            bp++;
        if (c == '\0') // a line with no final newline ends here
            break;
        *bp++ = '\0'; // mark end by overwriting the separator
    }
    pp[i] = NULL;
    nf[n] = i;
    return i;
}

//
// Field k of the line held for file n, or the empty string if the line is shorter than that.
// v7 handed the terminating NULL straight to strcmp() and to printf("%s").
//
static const char *fld(int n, int k)
{
    if (k < 0 || k >= nf[n])
        return "";
    return ppi[n][k];
}

//
// Print items from olist.
//
static void output(int on1, int on2)
{
    int i;
    const char *temp;

    if (no <= 0) { // default case
        printf("%s", on1 ? fld(F1, j1) : fld(F2, j2));
        for (i = 0; i < on1; i++)
            if (i != j1)
                printf("%c%s", sep1, fld(F1, i));
        for (i = 0; i < on2; i++)
            if (i != j2)
                printf("%c%s", sep1, fld(F2, i));
        printf("\n");
    } else {
        for (i = 0; i < no; i++) {
            temp = fld(olistf[i], olist[i]);
            if ((olistf[i] == F1 && on1 <= olist[i]) ||
                (olistf[i] == F2 && on2 <= olist[i]) || *temp == 0)
                temp = nullstr;
            printf("%s", temp);
            if (i == no - 1)
                printf("\n");
            else
                printf("%c", sep1);
        }
    }
}

static _Noreturn void error(const char *fmt, const char *arg)
{
    fprintf(stderr, "join: ");
    fprintf(stderr, fmt, arg);
    fprintf(stderr, "\n");
    exit(1);
}
