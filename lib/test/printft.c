/*
 * printft -- the formatting engine of phase 4: every conversion, every flag.
 *
 * The engine is not v7's.  v7's _doprnt was x86 assembly, so what is under test here
 * is the c-compiler's doprnt.c retargeted to a FILE * sink (lib/libc/stdio/doprnt.c),
 * and the things worth proving are the ones that changed on the way:
 *
 *   THE CASE FOLD IS GONE.  The original folded every conversion letter to upper
 *   case, because its output device printed KOI7 upper case only -- %x and %X were
 *   the same conversion and there was no lower-case hex at all.  So %x/%X, %e/%E and
 *   %g/%G are checked as three distinct PAIRS, and a fold would collapse each one.
 *
 *   %s OF A NULL WORD still reads as null.  A char * is fat, and a null one is not a
 *   zero word, so the engine reads the argument as a raw word before reinterpreting
 *   it; anything that went through char ** first would print an address.
 *
 *   THE LENGTH MODIFIERS ARE NOISE.  long == int and double == float, one word each,
 *   so %ld, %hd and %d must all print the same thing from the same argument.
 *
 *   snprintf RETURNS THE UNTRUNCATED LENGTH.  That falls out of the _IOSTRG sink:
 *   _flsbuf drops the byte and the engine goes on counting.  It is checked at, one
 *   below, and one above the buffer -- and at size 0, where not even the NUL may be
 *   written.
 *
 * The floating values are chosen to be exact in a 40-bit mantissa (0.5, 2.5, 1024.0),
 * so the digits below are the arithmetic's and not a rounding accident.  The exponent
 * range is this machine's, about 10^±18: there are no infinities, NaNs or denormals
 * to print (include/math.h).
 */
#include <stdio.h>
#include <string.h>

static int errors;

/* Compare a formatted line with what it should have been. */
static void chk(const char *want, const char *got)
{
    if (strcmp(got, want) == 0)
        printf("ok   [%s]\n", got);
    else {
        printf("FAIL [%s], want [%s]\n", got, want);
        errors++;
    }
}

static void eq(const char *what, int got, int want)
{
    if (got == want)
        printf("ok   %s %d\n", what, got);
    else {
        printf("FAIL %s %d, want %d\n", what, got, want);
        errors++;
    }
}

/* A v-form caller, so vsnprintf is reached across a real `...' boundary. */
static int vfmt(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

static void integers(void)
{
    char b[80];

    sprintf(b, "%d %i %u %o %x %X", 1234, -1234, 1234, 1234, 48879, 48879);
    chk("1234 -1234 1234 2322 beef BEEF", b);

    sprintf(b, "[%8d][%-8d][%08d]", 42, 42, 42);
    chk("[      42][42      ][00000042]", b);

    sprintf(b, "[%+d][%+d][% d][% d]", 42, -42, 42, -42);
    chk("[+42][-42][ 42][-42]", b);

    sprintf(b, "[%#o][%#x][%#X][%#o]", 8, 255, 255, 0);
    chk("[010][0xff][0XFF][0]", b);

    sprintf(b, "[%.5d][%8.5d][%-8.5d]", 42, 42, 42);
    chk("[00042][   00042][00042   ]", b);

    sprintf(b, "[%*d][%-*d][%.*d]", 6, 42, 6, 42, 5, 42);
    chk("[    42][42    ][00042]", b);

    /* A negative * width means left adjustment, as if the `-' flag were given. */
    sprintf(b, "[%*d]", -6, 42);
    chk("[42    ]", b);

    /* The length modifiers name a word, which is all there is. */
    sprintf(b, "%d %ld %hd %lld %zd", 7, 7, 7, 7, 7);
    chk("7 7 7 7 7", b);

    /* 41 signed value bits, 48 unsigned ones. */
    sprintf(b, "%d %u", -1099511627775L, 281474976710655U);
    chk("-1099511627775 281474976710655", b);
}

static void strings(void)
{
    char b[80];
    char *nullp;

    sprintf(b, "[%s][%10s][%-10s]", "abc", "abc", "abc");
    chk("[abc][       abc][abc       ]", b);

    sprintf(b, "[%.2s][%6.2s][%-6.2s]", "abcdef", "abcdef", "abcdef");
    chk("[ab][    ab][ab    ]", b);

    sprintf(b, "[%c][%3c][%-3c][%%]", 'z', 'z', 'z');
    chk("[z][  z][z  ][%]", b);

    nullp = NULL;
    sprintf(b, "[%s]", nullp);
    chk("[(null)]", b);

    /* An unknown conversion is echoed rather than swallowed. */
    sprintf(b, "[%q]", 0);
    chk("[%q]", b);
}

static void floats(void)
{
    char b[80];

    sprintf(b, "%f %f %f", 0.5, 2.5, 1024.0);
    chk("0.500000 2.500000 1024.000000", b);

    /*
     * %.0f of 2.5 is 3: the engine rounds a tie AWAY FROM ZERO, where a library
     * rounding to even would say 2.  §7.21.6.1p13 leaves the choice open.
     *
     * %010.2f keeps its zero fill.  A precision disarms the `0' flag only for the
     * INTEGER conversions (§7.21.6.1p6), and the engine this came from applied the
     * rule to floats as well, which is what the zeros below are guarding.
     */
    sprintf(b, "[%.0f][%.2f][%10.2f][%-10.2f][%010.2f]", 2.5, 2.5, 2.5, 2.5, 2.5);
    chk("[3][2.50][      2.50][2.50      ][0000002.50]", b);

    sprintf(b, "%f %.2f", -0.5, -2.5);
    chk("-0.500000 -2.50", b);

    sprintf(b, "%e %E", 1024.0, 1024.0);
    chk("1.024000e+03 1.024000E+03", b);

    sprintf(b, "%.2e %.0e", 0.5, 0.5);
    chk("5.00e-01 5e-01", b);

    sprintf(b, "%g %G %g", 1024.0, 0.5, 0.000001);
    chk("1024 0.5 1e-06", b);

    /* %g drops trailing zeros; %#g keeps them. */
    sprintf(b, "[%g][%#g]", 2.5, 2.5);
    chk("[2.5][2.50000]", b);

    /* Near the top of the exponent range, where nothing overflows to an infinity. */
    sprintf(b, "%e", 1e18);
    chk("1.000000e+18", b);
}

static void widths(void)
{
    char b[80];
    int n;

    /* sprintf returns the count, where v7's returned the buffer. */
    n = sprintf(b, "%s-%d", "abc", 42);
    eq("sprintf count", n, 6);
    chk("abc-42", b);

    /* snprintf: exactly, one short, one byte, and nothing at all. */
    n = snprintf(b, 7, "%s", "abcdef");
    eq("snprintf exact", n, 6);
    chk("abcdef", b);

    n = snprintf(b, 4, "%s", "abcdef");
    eq("snprintf truncated", n, 6);
    chk("abc", b);

    n = snprintf(b, 1, "%s", "abcdef");
    eq("snprintf one byte", n, 6);
    chk("", b);

    strcpy(b, "untouched");
    n = snprintf(b, 0, "%s", "abcdef");
    eq("snprintf size 0", n, 6);
    chk("untouched", b);

    /* The v-forms reach the same engine. */
    n = vfmt(b, sizeof b, "%s=%d", "x", 9);
    eq("vsnprintf count", n, 3);
    chk("x=9", b);

    /* %n reports how much has gone out so far. */
    sprintf(b, "abc%ndef", &n);
    eq("%n", n, 3);
}

int main(void)
{
    integers();
    strings();
    floats();
    widths();

    /* printf itself, not just sprintf: the same engine over a real stream. */
    printf("printf %d %s %.2f\n", 42, "direct", 2.5);
    fprintf(stdout, "fprintf %x\n", 255);
    puts("puts");
    fputs("fputs\n", stdout);
    putchar('c');
    putchar('\n');

    printf("%d error(s)\n", errors);
    return errors != 0;
}
