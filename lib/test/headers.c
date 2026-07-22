/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * The header tree itself: every header in ../../include, included TWICE.
 *
 * This is a compile-time test that happens to run.  What it proves is mostly in
 * the fact that it builds at all:
 *
 *   - every header is idempotent.  A second inclusion of a header whose guard is
 *     missing or misspelled redefines its typedefs and structs, and the compile
 *     fails.  Thirty-four of these headers had no guard until the C11 pass.
 *   - <assert.h> is the deliberate exception, and the double inclusion is how it
 *     is checked: it is NOT guarded, because C11 SS7.2 re-examines NDEBUG at every
 *     inclusion.  The second include below therefore has to redefine assert
 *     cleanly rather than complain, which is what its `#undef assert' is for.
 *   - no two headers collide.  time_t is declared in both <time.h> and
 *     <sys/types.h>, and NULL in <stddef.h> where <stdio.h> once had its own;
 *     both are included here for that reason.
 *
 * The runtime half then exercises one construct from each header that the C11
 * pass changed, where a wrong header would compile and misbehave rather than
 * fail: assert() as an EXPRESSION inside an if/else (v7's brace block would not
 * parse there), the conditional case fold, HUGE_VAL's magnitude, a format macro
 * from <inttypes.h>, and _Static_assert through its <assert.h> spelling.
 *
 * Build it once by hand with -DNDEBUG to cover the other arm of <assert.h>:
 *
 *      b6cc -I../../include -DNDEBUG -c headers.c
 *
 * clang-format is off over the inclusions and must stay off: it sorts an #include
 * block, and both the repetition and the sys/ ordering below are the test.
 */
/* clang-format off */
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fenv.h>
#include <inttypes.h>
#include <locale.h>
#include <math.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <uchar.h>
#include <varargs.h>
#include <wchar.h>
#include <wctype.h>

/*
 * The sys/ headers are NOT self-sufficient and are not meant to be: sys/dir.h
 * needs ino_t from sys/types.h and DIRSIZ from sys/param.h, and says at its head
 * that it will not default either -- one home only.  So they go in the order the
 * kernel includes them in, types before param before the rest, and that ordering
 * is itself part of what this test pins down.
 */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/dir.h>
#include <sys/stat.h>

/* Again, all of it.  See above. */
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fenv.h>
#include <inttypes.h>
#include <locale.h>
#include <math.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <uchar.h>
#include <varargs.h>
#include <wchar.h>
#include <wctype.h>

#include <sys/types.h>
#include <sys/param.h>
#include <sys/dir.h>
#include <sys/stat.h>

/*
 * <tgmath.h> is included last and alone: every one of its macros shadows a
 * <math.h> function name, so a header included after it would see the macro
 * rather than the function.  Real code has the same obligation.
 */
#include <tgmath.h>
/* clang-format on */

int write(int fd, const char *buf, int n);

/* The compile-time form of assert, under the <assert.h> spelling of it. */
static_assert(sizeof(int) == 6, "a word is six char-units");
static_assert(NBPW == 6, "and sys/param.h agrees");

static void put(const char *s)
{
    write(1, s, strlen(s));
}

static void ok(const char *what, int cond)
{
    put(cond ? "ok   " : "FAIL ");
    put(what);
    put("\n");
}

int main(void)
{
    time_t t = 0;
    struct tm tm;
    jmp_buf env;
    fenv_t fe;
    mbstate_t mbs;
    struct lconv *lc;
    div_t d;
    FILE *f;
    char16_t c16;

    /* Every type above is named so the declarations are not dead. */
    (void)t;
    (void)tm;
    (void)env;
    (void)fe;
    (void)mbs;
    (void)lc;
    (void)d;
    (void)f;
    (void)c16;

    /*
     * assert() as an expression in the true arm of an if/else.  v7's macro was a
     * BRACE BLOCK, so this exact shape was a syntax error: the `;' after it
     * closed the if, and the else had no if to belong to.
     */
    if (sizeof(int) == 6)
        assert(NBPW == 6);
    else
        ok("unreachable", 0);
    ok("assert is an expression", 1);

    /* The C11 conditional fold, and v7's unconditional pair beside it. */
    ok("toupper folds a letter", toupper('q') == 'Q');
    ok("toupper spares a digit", toupper('1') == '1');
    ok("_toupper is v7's", _toupper('q') == 'Q');

    /* The space: printing, not graphic, and blank. */
    ok("isprint of space", isprint(' ') != 0);
    ok("isgraph of space", isgraph(' ') == 0);
    ok("isblank of tab", isblank('\t') != 0);

    /*
     * HUGE_VAL is this machine's largest finite value, not the PDP-11's: v7's
     * HUGE was 1.7e38, which does not fit in a word at all.  1e18 does fit and
     * must be below it; 1e17 times ten must not overflow past it either.
     */
    ok("HUGE_VAL is this machine's", HUGE_VAL > 1e18 && HUGE == HUGE_VAL);
    ok("LOGHUGE follows it", LOGHUGE == 19);

    /* A format macro that exists only because int_fast32_t does. */
    put("PRIdFAST32 is \"");
    put(PRIdFAST32);
    put("\"\n");

    /* The C11 mandatory errno trio, EILSEQ being the one v7 lacked. */
    ok("errno trio", EDOM == 33 && ERANGE == 34 && EILSEQ == 35);

    /* The signal names C11 asks for, over v7's numbering. */
    ok("SIGABRT is SIGIOT", SIGABRT == SIGIOT && SIGABRT == 6);
    ok("SIG_ERR exists", SIG_ERR != SIG_DFL && SIG_ERR != SIG_IGN);

    /* setvbuf modes are not _flag bits; see the note in <stdio.h>. */
    ok("_IONBF is a mode", _IONBF != _IOUNBUF && _IOFBF == 0);

    /* One locale, and the degenerate float environment. */
    ok("one rounding mode", FE_TONEAREST == 0 && FE_ALL_EXCEPT == 0);
    ok("one multibyte char", MB_CUR_MAX == 1);

    put("headers ok\n");
    return 0;
}
