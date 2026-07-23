//
// scanft -- the scanning engine of phase 4, and the conversions either side of it.
//
// _doscan is v7's, and what is worth proving is where it stopped being v7's:
//
//   AN ARGUMENT IS A RAW WORD.  v7 walked the caller's parameter block as `int **'
//   and stored through it after a switch on (scale<<4)|size.  A char * is fat here
//   and an int * is not, so the two cannot share a C type; the word is carried
//   untyped and reinterpreted at the point of use.  %s and %d in the same call, each
//   storing correctly, is the check.
//
//   THE SIZE MODIFIERS COLLAPSE.  short == int == long == one word, and float ==
//   double == one word, so %d/%hd/%ld all store the same word and %f/%lf/%e all go
//   through atof().  v7's six-armed switch has two arms.
//
//   THE CLASS TABLE IS INDEXED SAFELY.  v7 wrote _sctab[getc(iop)] and asked
//   afterwards whether the value was EOF -- reading _sctab[-1] -- and its %[ walked a
//   char subscript into a 128-entry table.  An unterminated %[ and a class scan that
//   runs into end of file are here for that reason.
//
// scanf() itself is fed by pointing stdin at a file this program wrote, so the input
// is the program's own and nothing host-dependent reaches the .expected file.
//
// atof is the other half: v7's, with `big' cut from 2^56 to 2^40 because that is the
// width of this machine's mantissa (lib/libc/gen/atof.c).  Its inverses ecvt, fcvt
// and gcvt are v7 extensions no header declares, so they are declared here -- exactly
// as gcvt.c declares ecvt for itself.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *ecvt(double arg, int ndigits, int *decpt, int *sign);
char *fcvt(double arg, int ndigits, int *decpt, int *sign);
char *gcvt(double number, int ndigit, char *buf);

#define FNAME "scanft.tmp"

static int errors;

static void eq(const char *what, long got, long want)
{
    if (got == want)
        printf("ok   %s %ld\n", what, got);
    else {
        printf("FAIL %s %ld, want %ld\n", what, got, want);
        errors++;
    }
}

static void eqs(const char *what, const char *got, const char *want)
{
    if (strcmp(got, want) == 0)
        printf("ok   %s [%s]\n", what, got);
    else {
        printf("FAIL %s [%s], want [%s]\n", what, got, want);
        errors++;
    }
}

// Floating comparison, to the precision printf will show.
static void eqf(const char *what, double got, double want)
{
    char g[40], w[40];

    sprintf(g, "%.10g", got);
    sprintf(w, "%.10g", want);
    if (strcmp(g, w) == 0)
        printf("ok   %s %s\n", what, g);
    else {
        printf("FAIL %s %s, want %s\n", what, g, w);
        errors++;
    }
}

// A v-form caller, so vsscanf is reached across a real `...' boundary.
static int vscan(const char *str, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsscanf(str, fmt, ap);
    va_end(ap);
    return n;
}

static void integers(void)
{
    int a, b, c, n;
    long l;
    short h;

    n = sscanf("12 -34 0", "%d %d %d", &a, &b, &c);
    eq("sscanf %d count", n, 3);
    eq("  first", a, 12);
    eq("  second", b, -34);
    eq("  third", c, 0);

    n = sscanf("777 ff FF", "%o %x %x", &a, &b, &c);
    eq("sscanf bases count", n, 3);
    eq("  octal", a, 0777);
    eq("  hex lower", b, 255);
    eq("  hex upper", c, 255);

    // Every size modifier names the same one word.
    a = b = 0;
    h     = 0;
    l     = 0;
    n     = sscanf("5 6 7", "%hd %d %ld", &h, &a, &l);
    eq("sscanf sizes count", n, 3);
    eq("  %hd", h, 5);
    eq("  %d", a, 6);
    eq("  %ld", l, 7);

    // A field width stops the conversion early; * discards it entirely.
    n = sscanf("123456", "%2d%3d", &a, &b);
    eq("sscanf widths count", n, 2);
    eq("  first two", a, 12);
    eq("  next three", b, 345);

    a = b = -1;
    n     = sscanf("11 22 33", "%*d %d %d", &a, &b);
    eq("sscanf suppressed count", n, 2);
    eq("  after *", a, 22);
    eq("  and", b, 33);

    // A literal in the format has to match, and stops the scan when it does not.
    n = sscanf("x=9", "x=%d", &a);
    eq("sscanf literal count", n, 1);
    eq("  value", a, 9);
    n = sscanf("y=9", "x=%d", &a);
    eq("sscanf literal mismatch", n, 0);

    // Nothing to read at all is EOF, not zero.
    n = sscanf("", "%d", &a);
    eq("sscanf empty", n, -1);
}

static void strings(void)
{
    char s[40], t[40];
    int a, n;

    n = sscanf("  hello   world  ", "%s %s", s, t);
    eq("sscanf %s count", n, 2);
    eqs("  first", s, "hello");
    eqs("  second", t, "world");

    // %c takes exactly one character, white space included.
    s[0] = s[1] = 0;
    n           = sscanf(" ab", "%c%c", &s[0], &s[1]);
    eq("sscanf %c count", n, 2);
    eq("  first is a space", s[0], ' ');
    eq("  second is a", s[1], 'a');

    // %[ ] and its negation.
    n = sscanf("abc123def", "%[abc]%[0123456789]", s, t);
    eq("sscanf %[ count", n, 2);
    eqs("  class", s, "abc");
    eqs("  digits", t, "123");

    //
    // A `-' inside a class is an ORDINARY MEMBER, not a range: v7's _getccl knows
    // nothing of ranges and this port did not teach it any.  So %[0-9] is the three
    // characters `0', `-' and `9', which is what this reads.
    //
    n = sscanf("9-0x", "%[0-9]", s);
    eq("sscanf no ranges", n, 1);
    eqs("  literal 0, - and 9", s, "9-0");

    n = sscanf("abc,def", "%[^,],%s", s, t);
    eq("sscanf %[^ count", n, 2);
    eqs("  up to comma", s, "abc");
    eqs("  after comma", t, "def");

    // A class that runs to end of input, where v7 would index _sctab[-1].
    n = sscanf("xyz", "%[xyz]", s);
    eq("sscanf class to eof", n, 1);
    eqs("  all of it", s, "xyz");

    // One call mixing a fat char * and a plain int *.
    n = sscanf("tag 7", "%s %d", s, &a);
    eq("sscanf mixed count", n, 2);
    eqs("  string", s, "tag");
    eq("  number", a, 7);

    n = vscan("42 abc", "%d %s", &a, s);
    eq("vsscanf count", n, 2);
    eq("  number", a, 42);
    eqs("  string", s, "abc");
}

static void floats(void)
{
    double d, e;
    int n;

    n = sscanf("3.5 -0.25", "%lf %lf", &d, &e);
    eq("sscanf %lf count", n, 2);
    eqf("  first", d, 3.5);
    eqf("  second", e, -0.25);

    n = sscanf("1e3 -3.25e2", "%f %e", &d, &e);
    eq("sscanf exponents count", n, 2);
    eqf("  1e3", d, 1000.0);
    eqf("  -3.25e2", e, -325.0);

    // atof directly, over the whole range the native format has.
    eqf("atof 0", atof("0"), 0.0);
    eqf("atof 0.5", atof("0.5"), 0.5);
    eqf("atof -1024", atof("-1024"), -1024.0);
    eqf("atof leading space", atof("   2.5"), 2.5);
    eqf("atof +2.5", atof("+2.5"), 2.5);
    eqf("atof 1e18", atof("1e18"), 1e18);
    eqf("atof 1e-18", atof("1e-18"), 1e-18);
    eqf("atof junk", atof("abc"), 0.0);
}

static void conversions(void)
{
    char buf[40];
    char *p;
    int decpt, sign;

    p = ecvt(12.34, 5, &decpt, &sign);
    eqs("ecvt digits", p, "12340");
    eq("ecvt decpt", decpt, 2);
    eq("ecvt sign", sign, 0);

    p = ecvt(-0.001953125, 4, &decpt, &sign);
    eqs("ecvt small digits", p, "1953");
    eq("ecvt small decpt", decpt, -2);
    eq("ecvt small sign", sign, 1);

    p = fcvt(12.34, 3, &decpt, &sign);
    eqs("fcvt digits", p, "12340");
    eq("fcvt decpt", decpt, 2);

    eqs("gcvt 1024", gcvt(1024.0, 8, buf), "1024");
    eqs("gcvt 0.5", gcvt(0.5, 8, buf), ".5");
    eqs("gcvt -2.5", gcvt(-2.5, 8, buf), "-2.5");
}

// scanf() proper, reading a file this program wrote and then made stdin.
static void onstdin(void)
{
    FILE *f;
    char s[40];
    int a, b, n;
    double d;

    f = fopen(FNAME, "w");
    if (f == NULL) {
        printf("FAIL cannot write %s\n", FNAME);
        errors++;
        return;
    }
    fputs("100 200\nword 1.5\n", f);
    fclose(f);

    if (freopen(FNAME, "r", stdin) == NULL) {
        printf("FAIL cannot make %s the standard input\n", FNAME);
        errors++;
        return;
    }

    n = scanf("%d %d", &a, &b);
    eq("scanf count", n, 2);
    eq("  first", a, 100);
    eq("  second", b, 200);

    n = fscanf(stdin, "%s %lf", s, &d);
    eq("fscanf count", n, 2);
    eqs("  word", s, "word");
    eqf("  number", d, 1.5);

    // Past the end now.
    eq("scanf at eof", scanf("%d", &a), -1);

    fclose(stdin);
    remove(FNAME);
}

int main(void)
{
    integers();
    strings();
    floats();
    conversions();
    onstdin();

    printf("%d error(s)\n", errors);
    return errors != 0;
}
