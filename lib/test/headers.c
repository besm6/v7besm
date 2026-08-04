// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// The header tree itself: every header in ../../include, included TWICE.
//
// This is a compile-time test that happens to run.  What it proves is mostly in
// the fact that it builds at all:
//
//   - every header is idempotent.  A second inclusion of a header whose guard is
//     missing or misspelled redefines its typedefs and structs, and the compile
//     fails.  Thirty-four of these headers had no guard until the C11 pass.
//   - <assert.h> is the deliberate exception, and the double inclusion is how it
//     is checked: it is NOT guarded, because C11 SS7.2 re-examines NDEBUG at every
//     inclusion.  The second include below therefore has to redefine assert
//     cleanly rather than complain, which is what its `#undef assert' is for.
//   - no two headers collide.  time_t is declared in both <time.h> and
//     <sys/types.h>, and NULL in <stddef.h> where <stdio.h> once had its own;
//     both are included here for that reason.
//
// The runtime half then exercises one construct from each header that the C11
// pass changed, where a wrong header would compile and misbehave rather than
// fail: assert() as an EXPRESSION inside an if/else (v7's brace block would not
// parse there), the conditional case fold, HUGE_VAL's magnitude, a format macro
// from <inttypes.h>, and _Static_assert through its <assert.h> spelling.
//
// Build it once by hand with -DNDEBUG to cover the other arm of <assert.h>:
//
//      b6cc -I../../include -DNDEBUG -c headers.c
//
// clang-format is off over the inclusions and must stay off: it sorts an #include
// block, and it would fold the repetition below -- which IS the test -- into one copy
// of each name, and move <tgmath.h> out of last place.
//
// clang-format off
#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fenv.h>
#include <inttypes.h>
#include <locale.h>
#include <math.h>
#include <setjmp.h>
#include <sgtty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <uchar.h>
#include <unistd.h>
#include <varargs.h>
#include <wchar.h>
#include <wctype.h>
#include <curses.h>
#include <unctrl.h>

//
// The sys/ headers, in the order clang-format would sort them into -- which is
// deliberately NOT the v7 one: sys/dir.h comes ahead of the sys/param.h and
// sys/types.h it needs.  It compiles because each of them now includes what it
// uses, and requiring an include order is requiring something no compiler checks.
// <sys/user.h> beside <errno.h> above is a test in itself: the two carried
// separate copies of the errno numbering until <sys/errno.h> became its one home,
// and any translation unit naming both of these headers used to fail.
//
// THE LIST DOES NOT GROW WITHOUT CHECKING.  <sys/tty.h> could not join it until
// <sys/ttyio.h> existed: it and <sgtty.h> wrote out the same thirty-five names, and
// spelled XTABS as 006000 where <sgtty.h> said 06000 -- b6cpp rejects a macro
// redefinition whose replacement text is not character-identical.  <sgtty.h> is in
// the list above -- <curses.h> would have pulled it in anyway -- so the two of them
// standing here IS the assertion that the pair is fixed.
//
#include <sys/dir.h>
#include <sys/errno.h>
#include <sys/kctl.h>
#include <sys/param.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/tty.h>
#include <sys/ttyio.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/wait.h>

// Again, all of it.  See above.
#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fenv.h>
#include <inttypes.h>
#include <locale.h>
#include <math.h>
#include <setjmp.h>
#include <sgtty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <uchar.h>
#include <unistd.h>
#include <varargs.h>
#include <wchar.h>
#include <wctype.h>
#include <curses.h>
#include <unctrl.h>

#include <sys/dir.h>
#include <sys/errno.h>
#include <sys/kctl.h>
#include <sys/param.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/tty.h>
#include <sys/ttyio.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/wait.h>

//
// <tgmath.h> is included last and alone: every one of its macros shadows a
// <math.h> function name, so a header included after it would see the macro
// rather than the function.  Real code has the same obligation.
//
#include <tgmath.h>
// clang-format on

// The compile-time form of assert, under the <assert.h> spelling of it.
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

    // Every type above is named so the declarations are not dead.
    (void)t;
    (void)tm;
    (void)env;
    (void)fe;
    (void)mbs;
    (void)lc;
    (void)d;
    (void)f;
    (void)c16;

    //
    // assert() as an expression in the true arm of an if/else.  v7's macro was a
    // BRACE BLOCK, so this exact shape was a syntax error: the `;' after it
    // closed the if, and the else had no if to belong to.
    //
    if (sizeof(int) == 6)
        assert(NBPW == 6);
    else
        ok("unreachable", 0);
    ok("assert is an expression", 1);

    // The C11 conditional fold, and v7's unconditional pair beside it.
    ok("toupper folds a letter", toupper('q') == 'Q');
    ok("toupper spares a digit", toupper('1') == '1');
    ok("_toupper is v7's", _toupper('q') == 'Q');

    // The space: printing, not graphic, and blank.
    ok("isprint of space", isprint(' ') != 0);
    ok("isgraph of space", isgraph(' ') == 0);
    ok("isblank of tab", isblank('\t') != 0);

    //
    // HUGE_VAL is this machine's largest finite value, not the PDP-11's: v7's
    // HUGE was 1.7e38, which does not fit in a word at all.  1e18 does fit and
    // must be below it; 1e17 times ten must not overflow past it either.
    //
    ok("HUGE_VAL is this machine's", HUGE_VAL > 1e18 && HUGE == HUGE_VAL);
    ok("LOGHUGE follows it", LOGHUGE == 19);

    // A format macro that exists only because int_fast32_t does.
    put("PRIdFAST32 is \"");
    put(PRIdFAST32);
    put("\"\n");

    // The C11 mandatory errno trio, EILSEQ being the one v7 lacked.
    ok("errno trio", EDOM == 33 && ERANGE == 34 && EILSEQ == 35);

    // The signal names C11 asks for, over v7's numbering: 6 is v7's number for the
    // signal abort() raises, under C11's name for it -- v7's SIGIOT is not defined.
    ok("SIGABRT is 6", SIGABRT == 6);
    ok("SIG_ERR exists", SIG_ERR != SIG_DFL && SIG_ERR != SIG_IGN);

    // setvbuf modes are not _flag bits; see the note in <stdio.h>.
    ok("_IONBF is a mode", _IONBF != _IOUNBUF && _IOFBF == 0);

    // <unistd.h>: the three standard descriptors and the OR-able access modes.
    ok("stderr is descriptor 2", STDERR_FILENO == 2);
    ok("access modes are bits", (R_OK | W_OK | X_OK) == 7 && F_OK == 0);

    // <fcntl.h>: the three v7 open() modes, 0/1/2 as the kernel's ++mode expects.
    ok("open modes are 0/1/2", O_RDONLY == 0 && O_WRONLY == 1 && O_RDWR == 2);

    // One locale, and the degenerate float environment.
    ok("one rounding mode", FE_TONEAREST == 0 && FE_ALL_EXCEPT == 0);
    ok("one multibyte char", MB_CUR_MAX == 1);

    put("headers ok\n");
    return 0;
}
