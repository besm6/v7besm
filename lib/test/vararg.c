/*
 * vararg -- <varargs.h>, and the one-word argument.
 *
 * The header is now a shim over the compiler's <stdarg.h> (see include/varargs.h),
 * so what wants proving is the machine underneath it: an argument -- int or fat
 * char *, indifferently -- is exactly one 48-bit word, arguments sit in ascending
 * words of the caller's parameter block, and one va_arg steps exactly one of them.
 * A list that mixes the two types is therefore the test: if va_arg stepped by
 * sizeof(mode) the way the v7 header did, the ints would come out as addresses.
 *
 * It also hands a va_list to a second function, which is what vprintf will do in
 * phase 4, and it takes a char * back out of a list -- the pointer must still be
 * fat, or put() would walk nothing.
 *
 * Like hello.c it declares write() itself and carries its own output routines:
 * stdio is phase 4 and the string routines phase 2, and taking either from the
 * external c-compiler library instead would be the silent substitution
 * lib/README.md warns about.
 */
#include <varargs.h>

int write(int fd, char *buf, int n);

/* One string to the standard output, without stdio (phase 4). */
static void put(char *s)
{
    char *p = s;
    int n   = 0;

    while (*p) {
        p++;
        n++;
    }
    write(1, s, n);
}

/* One decimal digit, taken out of a literal: there is no itoa yet (phase 2). */
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

/*
 * n (int, char *) pairs, read from a list this function did not start -- the
 * vprintf shape.  A va_list is one word, so it passes as an ordinary argument.
 */
static void vreport(char *label, int n, va_list ap)
{
    int i;

    put(label);
    for (i = 0; i < n; i++) {
        put(" ");
        putnum(va_arg(ap, int));
        put("=");
        put(va_arg(ap, char *));
    }
    put("\n");
}

static void report(char *label, int n, ...)
{
    va_list ap;

    va_start(ap, n);
    vreport(label, n, ap);
    va_end(ap);
}

/* n ints, added up: the plain case, and the one that walks the most words. */
static int sum(int n, ...)
{
    va_list ap;
    int i, total = 0;

    va_start(ap, n);
    for (i = 0; i < n; i++)
        total += va_arg(ap, int);
    va_end(ap);
    return total;
}

int main(int argc, char **argv, char **envp)
{
    report("pairs", 3, 1, "one", 22, "twenty-two", -333, "minus");

    put("sum");
    put(" ");
    putnum(sum(0));
    put(" ");
    putnum(sum(3, 1, 2, 3));
    put(" ");
    putnum(sum(9, 1, 2, 3, 4, 5, 6, 7, 8, 9));
    put("\n");
    return 0;
}
