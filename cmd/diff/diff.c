/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// diff - differential file comparison
//
//	Uses an algorithm due to Harold Stone, which finds
//	a pair of longest identical subsequences in the two
//	files.
//
//	The major goal is to generate the match vector J.
//	J[i] is the index of the line in file1 corresponding
//	to line i file0.  J[i] = 0 if there is no
//	such line in file1.
//
//	Lines are hashed so as to work in core.  All potential
//	matches are located by sorting the lines of each file
//	on the hash (called `value').  In particular, this
//	collects the equivalence classes in file1 together.
//	Subroutine equiv() replaces the value of each line in
//	file0 by the index of the first element of its
//	matching equivalence in (the reordered) file1.
//	To save space equiv() squeezes file1 into a single
//	array `member' in which the equivalence classes
//	are simply concatenated, except that their first
//	members are flagged by changing sign.
//
//	Next the indices that point into `member' are unsorted into
//	array `class' according to the original order of file0.
//
//	The cleverness lies in routine stone().  This marches
//	through the lines of file0, developing a vector `klist'
//	of "k-candidates".  At step i a k-candidate is a matched
//	pair of lines x,y (x in file0 y in file1) such that
//	there is a common subsequence of length k
//	between the first i lines of file0 and the first y
//	lines of file1, but there is no such subsequence for
//	any smaller y.  x is the earliest possible mate to y
//	that occurs in such a subsequence.
//
//	Whenever any of the members of the equivalence class of
//	lines in file1 matable to a line in file0 has serial number
//	less than the y of some k-candidate, that k-candidate
//	with the smallest such y is replaced.  The new
//	k-candidate is chained (via `pred') to the current
//	k-1 candidate so that the actual subsequence can
//	be recovered.  When a member has serial number greater
//	than the y of all k-candidates, the klist is extended.
//	At the end, the longest subsequence is pulled out
//	and placed in the array J by unravel().
//
//	With J in hand, the matches there recorded are
//	checked against reality to assure that no spurious
//	matches have crept in due to hashing.  If they have,
//	they are broken, and "jackpot" is recorded -- a harmless
//	matter except that a true match for a spuriously
//	mated line may now be unnecessarily reported as a change.
//
//	Much of the complexity of the program comes simply
//	from trying to minimize core utilization and
//	maximize the range of doable problems by dynamically
//	allocating what is needed and reusing what is not.
//	The core requirements for problems larger than somewhat
//	are (in words) 2*length(file0) + length(file1) +
//	3*(number of k-candidates installed),  typically about
//	6n words for files of length n.
//
// One of task C5f's seven (../TODO.md).  ./README.md is the account; five things are worth
// having at the head of the source.
//
// THE LINE HASH WAS A 1's-COMPLEMENT SUM IN 16-BIT HUNKS OF A 32-BIT long, and neither
// number exists here: short == int == long == one 41-bit word (doc/Besm6_Data_Representation).
// The two `(short)' casts in readhash() are therefore no-ops, which is harmless -- any
// deterministic function will do, and check() verifies every match against the real text --
// but `sum' was UNBOUNDED, growing by up to 2^23 per byte, so a line past about 130 kilobytes
// overflowed a signed 41-bit word.  It is masked to 32 bits inside the loop now, which is
// sum(1)'s C5a finding from the other end: v7's portability there came from a mask inside
// the loop rather than from the register's width, and here the mask had to be added.
//
// AND readhash() ANSWERED `0' FOR END OF FILE, which is also a legitimate hash.  prepare()
// looped on it, so a line whose hash came out zero truncated the file silently.  End of file
// is the return value now and the hash goes back through a pointer.
//
// sort() FORMS A POINTER BELOW ITS ARRAY.  CACM #201 shellsort steps `ai -= m' past the base
// of a[] and detects it with `if (aim < ai) break;  /*wraparound*/' -- both `struct line *',
// so both are thin and neither is a §2 lowering problem, but forming the pointer is
// undefined and on a word-address machine the wraparound guard need not fire at all.  It is
// comm.c's `lb1 - 1' from task C5b and it takes the same fix: index arithmetic.
//
// `char c, d' IN check() AND AN UNBOUNDED skipline().  getc() answers EOF, which a char here
// truncates to 0377 -- so on a file whose last line has no newline, check()'s inner loop saw
// c == d == 0377 for ever and skipline() never found its '\n'.  v7 got away with it because
// its char was signed and EOF came back as -1... which the loop still never terminated on.
// Both are int and both test for EOF now.
//
// isspace() ON A RAW FILE BYTE, six call sites in the -b path.  lib/libc/gen/ctype_.c is 129
// entries, so `diff -b' over a Cyrillic file read past the end of the table.  The blank set
// is written out (§11).
//
// AND THE TEMP FILE.  `mktemp("/tmp/dXXXXX")' writes into a STRING LITERAL, and done()
// unlinked `tempfile' whether or not there was one -- unlink((char *)0) on every normal
// exit.  Both fixed.  Under b6sim /tmp is the BUILD MACHINE's (../README.md §9), so the `-'
// form is asserted under the booted kernel and nowhere else.
//
// NOT SETUID: it opens what the caller could open itself.
//
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define prints(s) fputs(s, stdout)

// v7 folded the hash in 16-bit halves of a 32-bit long.  Neither width exists here, so the
// mask is written down: it is what makes the accumulator bounded, not the register.
#define HALFLONG 16
#define FULLMASK 0xFFFFFFFF // what a PDP-11 `long' held
#define low(x)   ((x) & 0xFFFF)
#define high(x)  (((x) >> HALFLONG) & 0xFFFF)

static FILE *input[2];

struct cand {
    int x;
    int y;
    int pred;
};

struct line {
    int serial;
    int value;
};

static struct line *file[2];
static int len[2];
static struct line *sfile[2]; // shortened by pruning common prefix and suffix
static int slen[2];
static int pref, suff; // length of prefix and suffix
static int *class;     // will be overlaid on file[0]
static int *member;    // will be overlaid on file[1]
static int *klist;     // will be overlaid on file[0] after class
static struct cand *clist; // merely a free storage pot for candidates
static int clen = 0;
static int *J;      // will be overlaid on class
static long *ixold; // will be overlaid on klist
static long *ixnew; // will be overlaid on file[1]
static int opt;     // -1,0,1 = -e,normal,-f
static int status = 2;
static int anychange = 0;
static const char *empty = "";
static int bflag;

// v7 handed mktemp() a string literal to write into.
static char tempname[] = "/tmp/dXXXXX";
static char *tempfile;  // used when comparing against std input
static char *dummy;     // used in resetting storage search ptr

static _Noreturn void done(void);
static void onsig(int sig);
static char *talloc(int n);
static char *ralloc(char *p, int n);
static _Noreturn void noroom(void);
static void sortlines(struct line *a, int n);
static void unsort(struct line *f, int l, int *b);
static void filename(char **pa1, char **pa2);
static void prepare(int i, const char *arg);
static void prune(void);
static void equiv(struct line *a, int n, struct line *b, int m, int *c);
static int stone(const int *a, int n, const int *b, int *c);
static int newcand(int x, int y, int pred);
static int search(const int *c, int k, int y);
static void unravel(int p);
static void check(char **argv);
static int skipline(int f);
static void output(char **argv);
static void change(int a, int b, int c, int d);
static void range(int a, int b, const char *separator);
static void fetch(const long *f, int a, int b, FILE *lb, const char *s);
static int readhash(FILE *f, int *hp);
static void mesg(const char *s, const char *t);

//
// The blank set, written out rather than taken from <ctype.h>: see the head of this file.
// A FUNCTION and not a macro, deliberately.  The first draft of this port wrote it as a
// six-way `#define', and every call site here passes an expression WITH A SIDE EFFECT --
// `blank(c = getc(input[0]))' -- so the macro read six characters where isspace() had read
// one.  It cost an hour: `diff -b' then reported two identical files as wholly different and
// printed truncated text with it, because the byte offsets check() collects had run off.
// isspace() is a macro too and evaluates its argument ONCE (lib/libc/gen/ctype_.c); a
// hand-written replacement has to keep that promise.
//
static int blank(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

int main(int argc, char **argv)
{
    int k;
    char **args;

    args = argv;
    if (argc > 3 && *argv[1] == '-') {
        argc--;
        argv++;
        for (k = 1; argv[0][k]; k++) {
            switch (argv[0][k]) {
            case 'e':
                opt = -1;
                break;
            case 'f':
                opt = 1;
                break;
            case 'b':
                bflag = 1;
                break;
            case 'h':
                // -h REPLACES this program with diffh, passing our own argv.  See
                // ./README.md: it is the way out when the vectors below do not fit.
                execv("/usr/lib/diffh", args);
                mesg("cannot find diffh", empty);
                done();
            }
        }
    }
    if (argc != 3) {
        mesg("arg count", empty);
        done();
    }
    dummy = malloc(1);

    filename(&argv[1], &argv[2]);
    filename(&argv[2], &argv[1]);
    prepare(0, argv[1]);
    prepare(1, argv[2]);
    prune();
    sortlines(sfile[0], slen[0]);
    sortlines(sfile[1], slen[1]);

    member = (int *)file[1];
    equiv(sfile[0], slen[0], sfile[1], slen[1], member);
    member = (int *)ralloc((char *)member, (slen[1] + 2) * sizeof(int));

    class = (int *)file[0];
    unsort(sfile[0], slen[0], class);
    class = (int *)ralloc((char *)class, (slen[0] + 2) * sizeof(int));

    klist = (int *)talloc((slen[0] + 2) * sizeof(int));
    clist = (struct cand *)talloc(sizeof(struct cand));
    k     = stone(class, slen[0], member, klist);
    free((char *)member);
    free((char *)class);

    J = (int *)talloc((len[0] + 2) * sizeof(int));
    unravel(klist[k]);
    free((char *)clist);
    free((char *)klist);

    ixold = (long *)talloc((len[0] + 2) * sizeof(long));
    ixnew = (long *)talloc((len[1] + 2) * sizeof(long));
    check(argv);
    output(argv);
    status = anychange;
    done();
}

static _Noreturn void done(void)
{
    // v7 called unlink() whether or not there was a temp file.
    if (tempfile)
        unlink(tempfile);
    exit(status);
}

static void onsig(int sig)
{
    (void)sig;
    done();
}

static char *talloc(int n)
{
    char *p;

    p = malloc((unsigned)n);
    if (p != NULL)
        return p;
    noroom();
}

static char *ralloc(char *p, int n) // compacting reallocation
{
    char *q;

    free(p);
    free(dummy);
    dummy = malloc(1);
    q     = realloc(p, (unsigned)n);
    if (q == NULL)
        noroom();
    return q;
}

static _Noreturn void noroom(void)
{
    mesg("files too big, try -h\n", empty);
    done();
}

//
// Shellsort CACM #201.  v7 walked it with a `struct line *' that it deliberately stepped
// BELOW the array, catching the underflow with `if (aim < ai) break'.  Index arithmetic
// here: `idx > 0' is exactly `ai > a', and no pointer outside the array is ever formed.
//
static void sortlines(struct line *a, int n)
{
    struct line w;
    int j, m, k, idx;

    m = 0;
    for (j = 1; j <= n; j *= 2)
        m = 2 * j - 1;
    for (m /= 2; m != 0; m /= 2) {
        k = n - m;
        for (j = 1; j <= k; j++) {
            for (idx = j; idx > 0; idx -= m) {
                if (a[idx + m].value > a[idx].value ||
                    (a[idx + m].value == a[idx].value &&
                     a[idx + m].serial > a[idx].serial))
                    break;
                w.value          = a[idx].value;
                a[idx].value     = a[idx + m].value;
                a[idx + m].value = w.value;
                w.serial          = a[idx].serial;
                a[idx].serial     = a[idx + m].serial;
                a[idx + m].serial = w.serial;
            }
        }
    }
}

static void unsort(struct line *f, int l, int *b)
{
    int *a;
    int i;

    a = (int *)talloc((l + 1) * sizeof(int));
    for (i = 1; i <= l; i++)
        a[f[i].serial] = f[i].value;
    for (i = 1; i <= l; i++)
        b[i] = a[i];
    free((char *)a);
}

//
// If the first argument names a directory, replace it by the file of the second's name
// inside it; if it is `-', copy standard input to a temp file and use that.  v7 built the
// first path into malloc(100) with a hand-rolled unbounded copy (§6).
//
static void filename(char **pa1, char **pa2)
{
    char *a1, *b1;
    const char *a2, *base, *e;
    char buf[BUFSIZ];
    struct stat stbuf;
    int i, f, n;

    a1 = *pa1;
    a2 = *pa2;
    if (stat(a1, &stbuf) != -1 && ((stbuf.st_mode & S_IFMT) == S_IFDIR)) {
        base = a2;
        for (e = a2; *e; e++)
            if (*e == '/' && e[1] != 0 && e[1] != '/')
                base = e + 1;
        n  = strlen(a1) + 1 + strlen(base) + 1;
        b1 = talloc(n);
        strcpy(b1, a1);
        strcat(b1, "/");
        strcat(b1, base);
        *pa1 = b1;
    } else if (a1[0] == '-' && a1[1] == 0 && tempfile == 0) {
        signal(SIGHUP, onsig);
        signal(SIGINT, onsig);
        signal(SIGPIPE, onsig);
        signal(SIGTERM, onsig);
        *pa1 = tempfile = mktemp(tempname);
        if ((f = creat(tempfile, 0600)) < 0) {
            mesg("cannot create ", tempfile);
            done();
        }
        while ((i = read(0, buf, sizeof(buf))) > 0)
            write(f, buf, i);
        close(f);
    }
}

static void prepare(int i, const char *arg)
{
    struct line *p;
    int j, h;

    if ((input[i] = fopen(arg, "r")) == NULL) {
        mesg("cannot open ", arg);
        done();
    }
    p = (struct line *)talloc(3 * sizeof(struct line));
    // v7 wrote `for (j = 0; h = readhash(input[i]);)', which stops on a line whose hash is
    // zero as well as on end of file.
    for (j = 0; readhash(input[i], &h);) {
        p          = (struct line *)ralloc((char *)p, (++j + 3) * sizeof(struct line));
        p[j].value = h;
    }
    len[i]  = j;
    file[i] = p;
    fclose(input[i]);
}

static void prune(void)
{
    int i, j;

    for (pref = 0; pref < len[0] && pref < len[1] &&
                   file[0][pref + 1].value == file[1][pref + 1].value;
         pref++)
        ;
    for (suff = 0; suff < len[0] - pref && suff < len[1] - pref &&
                   file[0][len[0] - suff].value == file[1][len[1] - suff].value;
         suff++)
        ;
    for (j = 0; j < 2; j++) {
        sfile[j] = file[j] + pref;
        slen[j]  = len[j] - pref - suff;
        for (i = 0; i <= slen[j]; i++)
            sfile[j][i].serial = i;
    }
}

static void equiv(struct line *a, int n, struct line *b, int m, int *c)
{
    int i, j;

    i = j = 1;
    while (i <= n && j <= m) {
        if (a[i].value < b[j].value)
            a[i++].value = 0;
        else if (a[i].value == b[j].value)
            a[i++].value = j;
        else
            j++;
    }
    while (i <= n)
        a[i++].value = 0;
    b[m + 1].value = 0;
    j              = 0;
    while (++j <= m) {
        c[j] = -b[j].serial;
        while (b[j + 1].value == b[j].value) {
            j++;
            c[j] = b[j].serial;
        }
    }
    c[j] = -1;
}

static int stone(const int *a, int n, const int *b, int *c)
{
    int i, k, y;
    int j, l;
    int oldc, tc;
    int oldl;

    k    = 0;
    c[0] = newcand(0, 0, 0);
    for (i = 1; i <= n; i++) {
        j = a[i];
        if (j == 0)
            continue;
        y    = -b[j];
        oldl = 0;
        oldc = c[0];
        do {
            if (y <= clist[oldc].y)
                continue;
            l = search(c, k, y);
            if (l != oldl + 1)
                oldc = c[l - 1];
            if (l <= k) {
                if (clist[c[l]].y <= y)
                    continue;
                tc   = c[l];
                c[l] = newcand(i, y, oldc);
                oldc = tc;
                oldl = l;
            } else {
                c[l] = newcand(i, y, oldc);
                k++;
                break;
            }
        } while ((y = b[++j]) > 0);
    }
    return k;
}

static int newcand(int x, int y, int pred)
{
    struct cand *q;

    clist   = (struct cand *)ralloc((char *)clist, ++clen * sizeof(struct cand));
    q       = clist + clen - 1;
    q->x    = x;
    q->y    = y;
    q->pred = pred;
    return clen - 1;
}

static int search(const int *c, int k, int y)
{
    int i, j, l;
    int t;

    if (clist[c[k]].y < y) // quick look for typical case
        return k + 1;
    i = 0;
    j = k + 1;
    while ((l = (i + j) / 2) > i) {
        t = clist[c[l]].y;
        if (t > y)
            j = l;
        else if (t < y)
            i = l;
        else
            return l;
    }
    return l + 1;
}

static void unravel(int p)
{
    int i;
    struct cand *q;

    for (i = 0; i <= len[0]; i++)
        J[i] = i <= pref            ? i
               : i > len[0] - suff  ? i + len[1] - len[0]
                                    : 0;
    for (q = clist + p; q->y != 0; q = clist + q->pred)
        J[q->x + pref] = q->y + pref;
}

//
// check does double duty:
// 1.  ferret out any fortuitous correspondences due
//     to confounding by hashing (which result in "jackpot")
// 2.  collect random access indexes to the two files
//
static void check(char **argv)
{
    int i, j;
    int jackpot;
    long ctold, ctnew;
    int c, d; // v7 wrote `char', which truncates EOF and never terminates the loop below

    input[0] = fopen(argv[1], "r");
    input[1] = fopen(argv[2], "r");
    if (input[0] == NULL || input[1] == NULL) {
        mesg("cannot re-open the inputs", empty);
        done();
    }
    j           = 1;
    ixold[0] = ixnew[0] = 0;
    jackpot             = 0;
    ctold = ctnew = 0;
    for (i = 1; i <= len[0]; i++) {
        if (J[i] == 0) {
            ixold[i] = ctold += skipline(0);
            continue;
        }
        while (j < J[i]) {
            ixnew[j] = ctnew += skipline(1);
            j++;
        }
        for (;;) {
            c = getc(input[0]);
            d = getc(input[1]);
            ctold++;
            ctnew++;
            if (bflag && blank(c) && blank(d)) {
                do {
                    if (c == '\n')
                        break;
                    ctold++;
                    c = getc(input[0]);
                } while (blank(c));
                do {
                    if (d == '\n')
                        break;
                    ctnew++;
                    d = getc(input[1]);
                } while (blank(d));
            }
            if (c != d) {
                jackpot++;
                J[i] = 0;
                if (c != '\n')
                    ctold += skipline(0);
                if (d != '\n')
                    ctnew += skipline(1);
                break;
            }
            if (c == '\n' || c == EOF)
                break;
        }
        ixold[i] = ctold;
        ixnew[j] = ctnew;
        j++;
    }
    for (; j <= len[1]; j++) {
        ixnew[j] = ctnew += skipline(1);
    }
    (void)jackpot; // v7's `if (jackpot) mesg("jackpot", empty)' was commented out upstream
    fclose(input[0]);
    fclose(input[1]);
}

//
// Bytes from here to the end of the line, the newline included.  v7 looped until it saw a
// '\n' and so never stopped at end of file.
//
static int skipline(int f)
{
    int i, c;

    for (i = 1; (c = getc(input[f])) != '\n'; i++)
        if (c == EOF)
            return i - 1;
    return i;
}

static void output(char **argv)
{
    int m;
    int i0, i1, j1;
    int j0;

    input[0] = fopen(argv[1], "r");
    input[1] = fopen(argv[2], "r");
    if (input[0] == NULL || input[1] == NULL) {
        mesg("cannot re-open the inputs", empty);
        done();
    }
    m       = len[0];
    J[0]    = 0;
    J[m + 1] = len[1] + 1;
    if (opt != -1)
        for (i0 = 1; i0 <= m; i0 = i1 + 1) {
            while (i0 <= m && J[i0] == J[i0 - 1] + 1)
                i0++;
            j0 = J[i0 - 1] + 1;
            i1 = i0 - 1;
            while (i1 < m && J[i1 + 1] == 0)
                i1++;
            j1    = J[i1 + 1] - 1;
            J[i1] = j1;
            change(i0, i1, j0, j1);
        }
    else
        for (i0 = m; i0 >= 1; i0 = i1 - 1) {
            while (i0 >= 1 && J[i0] == J[i0 + 1] - 1 && J[i0] != 0)
                i0--;
            j0 = J[i0 + 1] - 1;
            i1 = i0 + 1;
            while (i1 > 1 && J[i1 - 1] == 0)
                i1--;
            j1    = J[i1 - 1] + 1;
            J[i1] = j1;
            change(i1, i0, j1, j0);
        }
    if (m == 0)
        change(1, 0, 1, len[1]);
}

static void change(int a, int b, int c, int d)
{
    if (a > b && c > d)
        return;
    anychange = 1;
    if (opt != 1) {
        range(a, b, ",");
        putchar(a > b ? 'a' : c > d ? 'd' : 'c');
        if (opt != -1)
            range(c, d, ",");
    } else {
        putchar(a > b ? 'a' : c > d ? 'd' : 'c');
        range(a, b, " ");
    }
    putchar('\n');
    if (opt == 0) {
        fetch(ixold, a, b, input[0], "< ");
        if (a <= b && c <= d)
            prints("---\n");
    }
    fetch(ixnew, c, d, input[1], opt == 0 ? "> " : empty);
    if (opt != 0 && c <= d)
        prints(".\n");
}

static void range(int a, int b, const char *separator)
{
    printf("%d", a > b ? b : a);
    if (a < b) {
        printf("%s%d", separator, b);
    }
}

static void fetch(const long *f, int a, int b, FILE *lb, const char *s)
{
    int i, j;
    int nc, c;

    for (i = a; i <= b; i++) {
        fseek(lb, f[i - 1], 0);
        nc = f[i] - f[i - 1];
        prints(s);
        for (j = 0; j < nc; j++) {
            if ((c = getc(lb)) == EOF)
                break;
            putchar(c);
        }
    }
}

//
// Hashing has the effect of arranging the line in 7-bit bytes and then summing 1's
// complement in 16-bit hunks.  Answers 0 at end of file and 1 otherwise, with the hash in
// *hp -- v7 returned the hash and used 0 for end of file, which a real line can produce.
//
static int readhash(FILE *f, int *hp)
{
    unsigned sum;
    unsigned shift;
    int space;
    int t;

    sum   = 1;
    space = 0;
    if (!bflag) {
        for (shift = 0; (t = getc(f)) != '\n'; shift += 7) {
            if (t == EOF)
                return 0;
            // v7 had no mask: sum grew by up to 2^23 a byte, so a long enough line overflowed
            // a 41-bit word here where it wrapped a 32-bit one there.
            sum = (sum + ((unsigned)t << (shift %= HALFLONG))) & FULLMASK;
        }
    } else {
        for (shift = 0;;) {
            t = getc(f);
            if (t == EOF)
                return 0;
            if (t == '\n')
                break;
            if (t == '\t' || t == ' ') {
                space++;
                continue;
            }
            if (space) {
                shift += 7;
                space = 0;
            }
            shift %= HALFLONG;
            sum = (sum + ((unsigned)t << shift)) & FULLMASK;
            shift += 7;
        }
    }
    sum = low(sum) + high(sum);
    *hp = (int)(low(sum) + high(sum));
    return 1;
}

static void mesg(const char *s, const char *t)
{
    fprintf(stderr, "diff: %s%s\n", s, t);
}
