//
// gen -- the small utilities of phase 2: abs, atoi, atol, rand, qsort, the ctype
// table, mktemp, isatty and perror.
//
// Three of these are more than a transcription of the v7 file and are what the program
// is really for:
//
//   qsort exchanges a WORD at a time when the element size is a multiple of six AND
//   the base is word-aligned, and falls back to the byte-wise exchange otherwise.  The
//   eligibility test is written in C as `a == (char *)(int *)a', which is an identity
//   only for a pointer already at byte #0, so it is worth proving from the outside:
//   the same data is sorted at element size 6, at 12, at 1, and from a base pushed one
//   byte off the word -- all four must come out identical.
//
//   rand() runs the v7 recurrence over an UNSIGNED word, since the state wraps at 2^48
//   here rather than at v7's 2^32 -- and yet the values pinned below are v7's own
//   16838, 5758, 10113, ... , because the returned bits lie inside the low 32 that a
//   truncating multiply carries along unchanged (gen/rand.c).  So this is a check on
//   b$umul as much as on rand: a product that saturated instead of wrapping, or an
//   arithmetic shift where a logical one was meant, would break the agreement.
//
//   the ctype table is indexed as `(_ctype_ + 1)[c]', which is fat-pointer arithmetic
//   over a byte-packed array -- six classes to a word -- and has to answer for EOF at
//   subscript -1 as well.  The sweep below counts each class over 0..127.
//
// perror writes to fd 2, which is why the harness captures stderr as well as stdout.
//
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

int write(int fd, char *buf, int n);
int open(char *path, int mode);
char *mktemp(char *as);
int isatty(int fd);
void perror(const char *s);
char *index(const char *sp, char c);

// One string to the standard output, without stdio (phase 4).
static void put(char *s)
{
    write(1, s, strlen(s));
}

static void putdigit(int d)
{
    char *p = "0123456789";

    while (d-- > 0)
        p++;
    write(1, p, 1);
}

static void putnum(int v)
{
    if (v < 0) {
        put("-");
        v = -v;
    }
    if (v >= 10)
        putnum(v / 10);
    putdigit(v % 10);
}

static void ok(char *what, int cond)
{
    put(cond ? "ok   " : "FAIL ");
    put(what);
    put("\n");
}

static void shownum(char *what, int v)
{
    put(what);
    put(" ");
    putnum(v);
    put("\n");
}

// qsort's comparison: one int per element, whatever the declared element size.
static int intcmp(const void *a, const void *b)
{
    return *(int *)a - *(int *)b;
}

// ... and over single bytes, for the element size the word-wise path must refuse.
static int bytecmp(const void *a, const void *b)
{
    return *(char *)a - *(char *)b;
}

static void showints(char *what, int *v, int n)
{
    int i;

    put(what);
    for (i = 0; i < n; i++) {
        put(" ");
        putnum(v[i]);
    }
    put("\n");
}

int main(int argc, char **argv, char **envp)
{
    int v[8], pairs[16], i, n;
    char bytes[9];
    char tmpl[16];
    char *p;

    // ---- abs ----
    ok("abs of a positive", abs(7) == 7);
    ok("abs of a negative", abs(-7) == 7);
    ok("abs of zero", abs(0) == 0);

    // ---- atoi and atol: one word each, so they cannot disagree ----
    shownum("atoi 1234", atoi("1234"));
    shownum("atoi -1234", atoi("-1234"));
    shownum("atoi +12", atoi("+12"));
    shownum("atoi with leading space", atoi("   42abc"));
    shownum("atoi of nothing", atoi("abc"));
    ok("atol agrees with atoi", atol("-987654") == atoi("-987654"));
    ok("atoi is not 16-bit", atoi("100000") == 100000);

    // ---- rand: the first values of the sequence, pinned ----
    srand(1);
    put("rand");
    for (i = 0; i < 5; i++) {
        put(" ");
        putnum(rand());
    }
    put("\n");
    srand(1);
    n = rand();
    srand(1);
    ok("srand replays the sequence", rand() == n);
    ok("rand stays in 0..077777", n >= 0 && n <= 077777);

    // ---- qsort, four ways over the same data ----
    v[0] = 5;
    v[1] = 3;
    v[2] = 9;
    v[3] = 1;
    v[4] = 3;
    v[5] = 8;
    v[6] = 0;
    v[7] = 2;
    qsort(v, 8, sizeof(int), intcmp);
    showints("qsort of words", v, 8);

    //
    // Element size 12: pairs, keyed on the first word.  The word-wise exchange moves
    // two words per element, so a swap that only moved one would show up as a value
    // separated from its tag.
    //
    for (i = 0; i < 8; i++) {
        pairs[2 * i]     = v[7 - i];
        pairs[2 * i + 1] = 100 + v[7 - i];
    }
    qsort(pairs, 8, 2 * sizeof(int), intcmp);
    showints("qsort of pairs", pairs, 16);

    // Element size 1: never eligible for the word-wise path.
    strcpy(bytes, "qsortabc");
    qsort(bytes, 8, 1, bytecmp);
    put("qsort of bytes \"");
    put(bytes);
    put("\"\n");

    //
    // The same bytes from a base one byte off the word boundary: es is 1 so the swap
    // is byte-wise either way, but this is also the only place the alignment test is
    // asked about a pointer that is NOT aligned.
    //
    strcpy(bytes + 1, "sortabc");
    qsort(bytes + 1, 7, 1, bytecmp);
    put("qsort unaligned \"");
    put(bytes + 1);
    put("\"\n");

    // ---- ctype: the classes counted over the whole ASCII range ----
    n = 0;
    for (i = 0; i < 128; i++)
        if (isalpha(i))
            n++;
    shownum("isalpha over 0..127", n);
    n = 0;
    for (i = 0; i < 128; i++)
        if (isdigit(i))
            n++;
    shownum("isdigit over 0..127", n);
    n = 0;
    for (i = 0; i < 128; i++)
        if (isspace(i))
            n++;
    shownum("isspace over 0..127", n);
    n = 0;
    for (i = 0; i < 128; i++)
        if (ispunct(i))
            n++;
    shownum("ispunct over 0..127", n);
    n = 0;
    for (i = 0; i < 128; i++)
        if (iscntrl(i))
            n++;
    shownum("iscntrl over 0..127", n);
    n = 0;
    for (i = 0; i < 128; i++)
        if (isxdigit(i))
            n++;
    shownum("isxdigit over 0..127", n);

    ok("isupper of A", isupper('A') != 0);
    ok("islower of A", islower('A') == 0);
    ok("isalnum of 9", isalnum('9') != 0);
    ok("isascii of 0200", isascii(0200) == 0);

    //
    // The space is the character the C11 pair is defined by (SS7.4.1.6, SS7.4.1.8):
    // printing but not graphic.  This assertion used to read `isprint(' ') == 0'
    // with the note "v7: space is _S, not _P" -- v7 had one macro for the two
    // classes and the space fell outside it.  The _B bit added to ctype_.c is
    // what separates them.
    //
    ok("isprint of space", isprint(' ') != 0);
    ok("isgraph of space", isgraph(' ') == 0);
    ok("isgraph of A", isgraph('A') != 0);
    ok("isblank", isblank(' ') && isblank('\t') && !isblank('\n') && !isblank('A'));

    ok("toupper", toupper('q') == 'Q');
    ok("tolower", tolower('Q') == 'q');

    //
    // The case v7's unconditional macros got wrong: neither is a letter, so each
    // must come back untouched (SS7.4.2).  v7's toupper('1') was '1' - 'a' + 'A',
    // which is not a character at all.  _toupper/_tolower still do that, and are
    // still valid for a caller that has already checked the class.
    //
    ok("toupper of non-letter", toupper('1') == '1' && toupper('[') == '[');
    ok("tolower of non-letter", tolower('1') == '1' && tolower('[') == '[');
    ok("_toupper unconditional", _toupper('q') == 'Q' && _tolower('Q') == 'q');

    // EOF is subscript -1, the leading zero the table carries for exactly this.
    ok("no class for EOF", !isalpha(-1) && !isdigit(-1) && !isspace(-1) && !iscntrl(-1));

    // ---- mktemp: the Xs become digits of the pid, and the name is free ----
    strcpy(tmpl, "/tmp/genXXXXXX");
    p = mktemp(tmpl);
    ok("mktemp returns its argument", p == tmpl);
    ok("mktemp kept the prefix", strncmp(tmpl, "/tmp/gen", 8) == 0);
    ok("mktemp replaced every X", index(tmpl, 'X') == 0);
    ok("mktemp kept the length", strlen(tmpl) == 14);
    ok("mktemp names nothing that exists", open(tmpl, 0) == -1);

    // ---- isatty: the harness redirects both descriptors to a file ----
    ok("isatty of a redirected stdout", isatty(1) == 0);
    ok("isatty of a bad descriptor", isatty(63) == 0);

    // ---- perror, on fd 2, after a call that really failed ----
    open("/no/such/file", 0);
    perror("perror");
    perror("");

    put("done\n");
    return 0;
}
