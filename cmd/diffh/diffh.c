/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// diffh -- differential file comparison over a bounded window.
//
//      diffh [ -b ] file1 file2
//
// The other half of diff(1), and it is not an optimisation: it is diff's WAY OUT OF MEMORY.
// diff reads both files whole, hashes every line and builds three integer vectors over them,
// and when malloc fails it says `files too big, try -h' and stops.  On a PDP-11 that took a
// large pair of files; here the whole user address space is 28,672 words (../README.md §6),
// so it is nearer than it was and -h matters more, not less.  ../TODO.md put the choice
// -- port this or drop -h -- and it is ported.
//
// diff -h REPLACES ITSELF with execv("/usr/lib/diffh", args), passing its own argv including
// the -h, so main() below has to tolerate a flag word it does not understand: it looks for a
// `b' in the first argument and ignores every other letter.  That is v7's arrangement and it
// is left exactly as it is; ../diff/README.md says why the path is /usr/lib.
//
// The algorithm is a sliding window: at most RANGE (30) lines of each file are in core at
// once, resynchronised on C (3) successive matching lines.  So its memory is bounded by a
// constant where diff's is bounded by the input, and it can compare files this machine could
// not otherwise hold -- at the price of missing any change longer than the window, which is
// what `can't resynchronize' says.
//
// Four things had to change and none is a §2:
//
//   `return;' FROM int main -- a C11 constraint violation (6.8.6.4), not a style point.
//
//   `%ld' FOUR TIMES.  ../README.md §3: `l' is parsed and ignored here, so it means nothing;
//   long is int is one 41-bit word.
//
//   dopen() BUILT A PATH INTO char b[100] with two hand-rolled unbounded copies, from two
//   argv strings.  §6's recurring finding.
//
//   isspace() ON A RAW FILE BYTE, four call sites in cmp()'s -b path.  lib/libc/gen/ctype_.c
//   is 129 entries and says outright that only isascii() may be applied to a byte above
//   0177, so `diffh -b' over a Cyrillic file read off the end of the table.  The blank set
//   is written out instead, which also makes a byte above 0177 an ordinary text character
//   (§11) rather than whatever the table happens to hold past its end.
//
// NOT SETUID: it opens what the caller could open itself.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define C     3
#define RANGE 30
#define LEN   255
#define INF   16384


static char *text[2][RANGE];
static long lineno[2] = { 1, 1 }; // no. of 1st stored line in each file
static int ntext[2];              // number of stored lines in each
static long n0, n1;               // scan pointer in each
static int bflag;
static int debug = 0;
static FILE *file[2];

static char *getl(int f, long n);
static void clrl(int f, long n);
static void movstr(const char *s, char *t);
static int easysynch(void);
static int output(int a, int b);
static void change(long a, int b, long c, int d, const char *s);
static void range(long a, int b);
static int cmp(const char *s, const char *t);
static FILE *dopen(const char *f1, const char *f2);
static _Noreturn void progerr(const char *s);
static _Noreturn void error(const char *s, const char *t);
static int hardsynch(void);

//
// The blank set, written out rather than taken from <ctype.h>.  A FUNCTION and not a macro:
// cmp() below writes `while (blank(*++s))', and a six-way macro would advance the cursor six
// times per test.  isspace() is a macro and evaluates its argument once; a hand-written
// replacement has to keep that promise.  ../diff/README.md is the account -- the first draft
// of this port had it as a macro and `diff -b' came out wrong.
//
static int blank(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

// return pointer to line n of file f
static char *getl(int f, long n)
{
    char *t;
    int delta, nt;

again:
    delta = n - lineno[f];
    nt    = ntext[f];
    if (delta < 0)
        progerr("1");
    if (delta < nt)
        return text[f][delta];
    if (delta > nt)
        progerr("2");
    if (nt >= RANGE)
        progerr("3");
    if (feof(file[f]))
        return NULL;
    t = text[f][nt];
    if (t == NULL) {
        t = text[f][nt] = malloc(LEN + 1);
        if (t == NULL) {
            if (hardsynch())
                goto again;
            else
                progerr("5");
        }
    }
    t = fgets(t, LEN, file[f]);
    if (t != NULL)
        ntext[f]++;
    return t;
}

// remove thru line n of file f from storage
static void clrl(int f, long n)
{
    int i, j;

    j = n - lineno[f] + 1;
    for (i = 0; i + j < ntext[f]; i++)
        movstr(text[f][i + j], text[f][i]);
    lineno[f] = n + 1;
    ntext[f] -= j;
}

static void movstr(const char *s, char *t)
{
    while ((*t++ = *s++) != '\0')
        continue;
}

int main(int argc, char **argv)
{
    char *s0 = NULL, *s1 = NULL;

    if (argc > 1 && *argv[1] == '-') {
        argc--;
        argv++;
        // diff -h execv's us with its OWN argv, -h included, so anything but `b' is ignored.
        while (*++argv[0])
            if (*argv[0] == 'b')
                bflag++;
    }
    if (argc != 3)
        error("must have 2 file arguments", "");
    file[0] = dopen(argv[1], argv[2]);
    file[1] = dopen(argv[2], argv[1]);
    for (;;) {
        s0 = getl(0, ++n0);
        s1 = getl(1, ++n1);
        if (s0 == NULL || s1 == NULL)
            break;
        if (cmp(s0, s1) != 0) {
            if (!easysynch() && !hardsynch())
                progerr("5");
        } else {
            clrl(0, n0);
            clrl(1, n1);
        }
    }
    if (s0 == NULL && s1 == NULL)
        return 0; // v7 wrote a bare `return;' from a function returning int
    if (s0 == NULL)
        output(-1, INF);
    if (s1 == NULL)
        output(INF, -1);
    return 1;
}

// synch on C successive matches
static int easysynch(void)
{
    int i, j;
    int k, m;
    char *s0, *s1;

    for (i = j = 1; i < RANGE && j < RANGE; i++, j++) {
        s0 = getl(0, n0 + i);
        if (s0 == NULL)
            return output(INF, INF);
        for (k = C - 1; k < j; k++) {
            for (m = 0; m < C; m++)
                if (cmp(getl(0, n0 + i - m), getl(1, n1 + k - m)) != 0)
                    goto cont1;
            return output(i - C, k - C);
        cont1:;
        }
        s1 = getl(1, n1 + j);
        if (s1 == NULL)
            return output(INF, INF);
        for (k = C - 1; k <= i; k++) {
            for (m = 0; m < C; m++)
                if (cmp(getl(0, n0 + k - m), getl(1, n1 + j - m)) != 0)
                    goto cont2;
            return output(k - C, j - C);
        cont2:;
        }
    }
    return 0;
}

static int output(int a, int b)
{
    int i;
    char *s;

    if (a < 0)
        change(n0 - 1, 0, n1, b, "a");
    else if (b < 0)
        change(n0, a, n1 - 1, 0, "d");
    else
        change(n0, a, n1, b, "c");
    for (i = 0; i <= a; i++) {
        s = getl(0, n0 + i);
        if (s == NULL)
            break;
        printf("< %s", s);
        clrl(0, n0 + i);
    }
    n0 += i - 1;
    if (a >= 0 && b >= 0)
        printf("---\n");
    for (i = 0; i <= b; i++) {
        s = getl(1, n1 + i);
        if (s == NULL)
            break;
        printf("> %s", s);
        clrl(1, n1 + i);
    }
    n1 += i - 1;
    return 1;
}

static void change(long a, int b, long c, int d, const char *s)
{
    range(a, b);
    printf("%s", s);
    range(c, d);
    printf("\n");
}

static void range(long a, int b)
{
    if (b == INF)
        printf("%d,$", a);
    else if (b == 0)
        printf("%d", a);
    else
        printf("%d,%d", a, a + b);
}

static int cmp(const char *s, const char *t)
{
    if (debug)
        printf("%s:%s\n", s, t);
    for (;;) {
        if (bflag && blank(*s) && blank(*t)) {
            while (blank(*++s))
                ;
            while (blank(*++t))
                ;
        }
        if (*s != *t || *s == 0)
            break;
        s++;
        t++;
    }
    return *s - *t;
}

//
// Open f1, or -- if f1 names a directory -- the file of f2's name inside it.  v7 built that
// path into char[100] with two hand-rolled copies and no bound at all (§6).
//
static FILE *dopen(const char *f1, const char *f2)
{
    FILE *f;
    char b[512];
    const char *base, *e;
    struct stat statbuf;

    if (cmp(f1, "-") == 0) {
        if (cmp(f2, "-") == 0)
            error("can't do - -", "");
        else
            return stdin;
    }
    if (stat(f1, &statbuf) == -1)
        error("can't access ", f1);
    if ((statbuf.st_mode & S_IFMT) == S_IFDIR) {
        base = f2;
        for (e = f2; *e; e++)
            if (*e == '/' && e[1] != 0 && e[1] != '/')
                base = e + 1;
        if (strlen(f1) + 1 + strlen(base) + 1 > sizeof(b))
            error("path too long: ", f1);
        strcpy(b, f1);
        strcat(b, "/");
        strcat(b, base);
        f1 = b;
    }
    f = fopen(f1, "r");
    if (f == NULL)
        error("can't open ", f1);
    return f;
}

static _Noreturn void progerr(const char *s)
{
    error("program error ", s);
}

static _Noreturn void error(const char *s, const char *t)
{
    fprintf(stderr, "diffh: %s%s\n", s, t);
    exit(1);
}

// stub for resynchronization beyond limits of text buf
static int hardsynch(void)
{
    change(n0, INF, n1, INF, "c");
    printf("---change record omitted\n");
    error("can't resynchronize", "");
    return 0;
}
