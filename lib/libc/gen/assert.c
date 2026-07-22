/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * The handler a failed assert() calls.
 *
 * v7 had no such function: its assert() macro expanded to an fprintf and an exit
 * inline, which is why v7's <assert.h> was a brace block and would not sit in an
 * if/else (see the header for the rest of that story).  One function instead, so
 * the macro can be the void EXPRESSION C11 SS7.2.1.1 asks for.
 *
 * Straight to fd 2 with write(), not through stdio: there is no stdio until
 * phase 4, and an assertion failure is exactly the moment not to depend on one --
 * a buffered stream would be the thing most likely to be broken already.  perror()
 * writes the same way and for the same reason.
 *
 * abort(), not exit(1): SS7.2.1.1 says the handler calls abort, and v7's exit(1)
 * was indistinguishable from an ordinary failure.  abort() raises SIGIOT, which
 * this kernel will dump core for once signal delivery lands in phase 6; until
 * then it is still the loudest available way to stop.
 */
#include <assert.h> /* for the declaration this must match */
#include <string.h>

int write(int fd, const char *buf, int n);
_Noreturn void abort(void);

/* Decimal, without stdio: only the line number needs it. */
static void putnum(int v)
{
    char buf[12];
    char *p = buf + sizeof(buf);

    if (v < 0) {
        write(2, "-", 1);
        v = -v;
    }
    do {
        *--p = '0' + v % 10;
        v /= 10;
    } while (v);
    write(2, p, (int)(buf + sizeof(buf) - p));
}

static void put(const char *s)
{
    write(2, s, strlen(s));
}

_Noreturn void __assert_fail(const char *expr, const char *file, int line)
{
    put("Assertion failed: ");
    put(expr);
    put(", file ");
    put(file);
    put(", line ");
    putnum(line);
    put("\n");
    abort();
    /*NOTREACHED*/
}
