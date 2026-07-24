//
// strtolt -- strtol and strtoul: the value, the unconverted tail, and errno.
//
// v7 had neither, so these are 4.4BSD's scanner retargeted (lib/libc/gen/strtol.c),
// and what is worth proving is what the retargeting had to touch:
//
//   THE LIMITS ARE THIS MACHINE's, and they are asymmetric.  `long' is one word,
//   41 bits signed, so LONG_MAX is 2^40-1 and LONG_MIN is -2^40 -- one further out
//   than the positive end -- while `unsigned long' is the whole 48-bit word and
//   ULONG_MAX is 2^48-1.  Every one of those four boundaries is checked at the
//   limit and one past it: -1099511627776 must convert exactly and NOT set ERANGE,
//   and it is the value the implementation returns as the constant, because 2^40 is
//   the one magnitude no `long' can negate.
//
//   NO SIGN IS EVER REINTERPRETED.  A negative integer here is a 41-bit two's
//   complement field with bits 48..42 zero, so casting one to `unsigned long' does
//   NOT widen it -- (unsigned long)-1L is 2^41-1, not ULONG_MAX.  BSD's cutoff and
//   result both ride such a cast; the port does not, and strtoul("-1") == ULONG_MAX
//   is the check that the negation happened in unsigned arithmetic instead.
//
//   "0x" WITH NO DIGIT AFTER IT converts the "0" and leaves the tail at the `x'
//   (C11 §7.22.1.4p4).  BSD reports the whole input unconverted there; this one
//   does not, and the three "0x"/"0xg" rows are that fix.
//
//   A BAD BASE is EINVAL rather than a nonsense answer -- undefined in C11,
//   permitted by POSIX.
//
// The tail is printed as `end +n', the offset endptr reached, which is a fat-pointer
// difference (b$pdiff) and not an address, so nothing host-dependent reaches the
// expected output.  errno is checked on every row, including the rows that must
// leave it alone: neither function ever clears it.
//
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

static int errors;

//
// printf cannot print LONG_MIN: the engine negates the value into an unsigned and
// -LONG_MIN does not exist in 41 bits (lib/libc/stdio/doprnt.c), so it comes out
// mangled.  Name it instead -- it is the interesting value here anyway.
//
static void shownum(long v)
{
    if (v == LONG_MIN)
        printf("LONG_MIN");
    else
        printf("%ld", v);
}

// One strtol case: value, tail offset and errno, all three at once.
static void tl(const char *s, int base, long want, int wantend, int wanterr)
{
    char *e;
    long v;
    int end, err;

    errno = 0;
    v     = strtol(s, &e, base);
    err   = errno; // before printf: the first one calls isatty and leaves ENOTTY behind
    end   = (int)(e - s);
    if (v == want && end == wantend && err == wanterr) {
        printf("ok   strtol(\"%s\", %d) = ", s, base);
        shownum(v);
        printf(" end +%d errno %d\n", end, err);
        return;
    }
    printf("FAIL strtol(\"%s\", %d) = ", s, base);
    shownum(v);
    printf(" end +%d errno %d, want ", end, err);
    shownum(want);
    printf(" end +%d errno %d\n", wantend, wanterr);
    errors++;
}

// The same for strtoul, whose values need no naming: %lu prints the whole word.
static void tu(const char *s, int base, unsigned long want, int wantend, int wanterr)
{
    char *e;
    unsigned long v;
    int end, err;

    errno = 0;
    v     = strtoul(s, &e, base);
    err   = errno;
    end   = (int)(e - s);
    if (v == want && end == wantend && err == wanterr) {
        printf("ok   strtoul(\"%s\", %d) = %lu end +%d errno %d\n", s, base, v, end, err);
        return;
    }
    printf("FAIL strtoul(\"%s\", %d) = %lu end +%d errno %d, want %lu end +%d errno %d\n", s, base,
           v, end, err, want, wantend, wanterr);
    errors++;
}

static void ok(const char *what, int cond)
{
    printf("%s %s\n", cond ? "ok  " : "FAIL", what);
    if (!cond)
        errors++;
}

int main(void)
{
    // ---- decimal, sign, white space, and the nothing-converted cases ----
    tl("1234", 10, 1234, 4, 0);
    tl("-1234", 10, -1234, 5, 0);
    tl("+12", 10, 12, 3, 0);
    tl("   42abc", 10, 42, 5, 0);
    tl("abc", 10, 0, 0, 0);
    tl("", 10, 0, 0, 0);
    tl("   ", 10, 0, 0, 0);
    tl("-", 10, 0, 0, 0);
    tl("0", 10, 0, 1, 0);

    // ---- an explicit base, and a digit that the base does not have ----
    tl("777", 8, 511, 3, 0);
    tl("0777", 8, 511, 4, 0);
    tl("ff", 16, 255, 2, 0);
    tl("FF", 16, 255, 2, 0);
    tl("0xff", 16, 255, 4, 0);
    tl("0XfF", 16, 255, 4, 0);
    tl("1011", 2, 11, 4, 0);
    tl("z", 36, 35, 1, 0);
    tl("Zz", 36, 1295, 2, 0);
    tl("129", 9, 11, 2, 0);

    // ---- base 0: 0x is hex, a leading 0 is octal, anything else decimal ----
    tl("0", 0, 0, 1, 0);
    tl("077", 0, 63, 3, 0);
    tl("09", 0, 0, 1, 0);
    tl("0x1f", 0, 31, 4, 0);
    tl("18", 0, 18, 2, 0);
    tl("-0x10", 0, -16, 5, 0);
    tl(" -0X1f", 0, -31, 6, 0);

    // ---- a prefix with no digit behind it converts the 0 and stops at the x ----
    tl("0x", 16, 0, 1, 0);
    tl("0x", 0, 0, 1, 0);
    tl("0xg", 0, 0, 1, 0);

    // ---- the ends of a 41-bit signed word, and one step past each ----
    tl("1099511627775", 10, LONG_MAX, 13, 0);
    tl("1099511627776", 10, LONG_MAX, 13, ERANGE);
    tl("-1099511627776", 10, LONG_MIN, 14, 0);
    tl("-1099511627777", 10, LONG_MIN, 14, ERANGE);
    tl("999999999999999999", 10, LONG_MAX, 18, ERANGE);
    tl("0xffffffffff", 0, LONG_MAX, 12, 0);
    tl("0x10000000000", 0, LONG_MAX, 13, ERANGE);

    // ---- a base outside 0 and 2..36 ----
    tl("10", 1, 0, 0, EINVAL);
    tl("10", 37, 0, 0, EINVAL);
    tl("10", -5, 0, 0, EINVAL);

    // ---- strtoul: the whole 48-bit word, and a `-' that negates rather than fails ----
    tu("0", 10, 0, 1, 0);
    tu("42", 10, 42, 2, 0);
    tu("4294967296", 10, 4294967296UL, 10, 0);
    tu("281474976710655", 10, ULONG_MAX, 15, 0);
    tu("281474976710656", 10, ULONG_MAX, 15, ERANGE);
    tu("-1", 10, ULONG_MAX, 2, 0);
    tu("-2", 10, 281474976710654UL, 2, 0);
    tu("0xffffffffffff", 0, ULONG_MAX, 14, 0);
    tu("0x1000000000000", 0, ULONG_MAX, 15, ERANGE);
    tu("0777", 0, 511, 4, 0);
    tu("zz", 36, 1295, 2, 0);
    tu("10", 1, 0, 0, EINVAL);

    // ---- the rest of the contract ----
    ok("every white-space character is skipped", strtol(" \t\n\v\f\r-8", (char **)0, 10) == -8);
    ok("a null endptr is accepted", strtol("42", (char **)0, 10) == 42);
    ok("a null endptr is accepted by strtoul", strtoul("42", (char **)0, 10) == 42);
    ok("strtol agrees with atoi", strtol("-987654", (char **)0, 10) == atoi("-987654"));
    errno = EDOM;
    strtol("5", (char **)0, 10);
    strtoul("5", (char **)0, 10);
    ok("errno is left alone on success", errno == EDOM);

    printf("%d error%s\n", errors, errors == 1 ? "" : "s");
    return 0;
}
