//
// curstty -- lib/libcurses' tty modes, on the booted kernel only.
//
// THIS IS THE HALF cursest.c HAS TO SWITCH OFF.  That program sets My_term so that initscr()
// never calls gettmode(), because gettmode() is an ioctl and the two harnesses do not hold
// the same terminal: under the booted kernel it reaches kernel/dev/tty.c's ttioccomm() and
// really reads and writes the console, while under b6sim the descriptor is one ctest
// redirected -- a file, not a terminal -- and the call comes back ENOTTY.  A shared
// expectation cannot say two things, so the tty modes have a program of their own and it is
// IMAGEONLY -- the same reason memt and shellt are.
//
// UNTIL THE PORT OF more(1) THE REASON WAS WORSE THAN THAT: b6sim's ioctl answered every
// request with success and changed nothing, and its gtty zero-filled FIVE words into a
// structure that is TWO (cmd/sim/tty.h, and section 4 of doc/Besm6_Data_Representation.md
// for why).  Both are now real -- b6sim translates struct sgttyb against the host's termios --
// so what separates the two worlds here is the machine underneath and nothing else.
//
// WHAT IT ASSERTS IS THE CONSOLE.  kernel/dev/sc.c opens it ECHO|CRMOD|XTABS, which is
// 010|020|06000 = 06030, and gettmode() must read exactly that back, save it in _res_flg,
// derive GT from XTABS being present (so FALSE) and NONL from CRMOD being present (so also
// FALSE), and then clear XTABS in the live flags.  If any of those disagrees, the
// disagreement is with the kernel's console driver and one of the two is wrong.
//
// IT RAN LAST IN THE DELETED kernel/test/libtest.sh, ON PURPOSE -- nothing runs it now.
// gettmode() clears XTABS on the real
// console and only endwin()'s resetty() puts it back, so if this program ever dies the only
// thing still running against a changed console is one echo.
//
// The terminal database is argv[1], and the environment is this program's own, both for
// termcapt.c's reasons.
//
#include <curses.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int errors;

static char *envvec[2];
static char tcvar[1024];

// The library writes through _putchar; swallow it.  What this program is about is the flag
// words, not the escape stream -- cursest.c has that -- and the capability strings for
// whatever TERM happens to be would make the expectation terminal-dependent.
int _putchar(int c)
{
    return 0;
}

static void ck(char *what, int got, int want)
{
    printf("%-30s 0%o", what, got);
    if (got != want) {
        printf("  WRONG, wanted 0%o", want);
        errors++;
    }
    printf("\n");
}

static void show(char *what)
{
    printf("%-30s flags=0%-6o echoit=%d rawmode=%d pfast=%d\n", what, _tty.sg_flags & 0177777,
           _echoit, _rawmode, _pfast);
}

// The console as kernel/dev/sc.c's scopen() leaves it.
#define CONSOLE_FLAGS (ECHO | CRMOD | XTABS)

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: curstty /path/to/termcap\n");
        return 1;
    }
    strcpy(tcvar, "TERMCAP=");
    strncat(tcvar, argv[1], sizeof(tcvar) - 10);
    envvec[0] = tcvar;
    envvec[1] = 0;
    environ   = envvec;

    // My_term is NOT set: this is initscr()'s other path, the one that hunts for a tty,
    // reads the modes and consults $TERM.  Def_term is what it falls back to, and it is
    // named here so that a $TERM in the environment cannot change the answer -- there is
    // none, since the environment above has one entry, but saying so costs nothing.
    Def_term = "vt100";

    printf("--- before initscr\n");
    ck("_tty_ch", _tty_ch, 1); // curses.c's default, until the isatty search runs

    if (initscr() == NULL) {
        printf("initscr failed\n");
        return 1;
    }

    printf("--- after initscr\n");
    // stdout was redirected to a file by the image-side runner and stdin was the console, so the isatty
    // search lands on descriptor 0.
    ck("_tty_ch", _tty_ch, 0);
    ck("_res_flg", _res_flg, CONSOLE_FLAGS);
    ck("sg_flags (XTABS cleared)", _tty.sg_flags, CONSOLE_FLAGS & ~XTABS);
    ck("GT (no XTABS on console)", GT, 0);
    ck("NONL (CRMOD on console)", NONL, 0);
    ck("_pfast", _pfast, 0);
    printf("%-30s %d\n", "LINES", LINES);
    printf("%-30s %d\n", "COLS", COLS);

    printf("--- mode changes\n");
    show("initial");
    cbreak();
    show("cbreak");
    ck("cbreak set CBREAK", _tty.sg_flags & CBREAK, CBREAK);
    ck("cbreak set _rawmode", _rawmode, 1);
    nocbreak();
    show("nocbreak");
    ck("nocbreak cleared CBREAK", _tty.sg_flags & CBREAK, 0);

    noecho();
    show("noecho");
    ck("noecho cleared ECHO", _tty.sg_flags & ECHO, 0);
    ck("noecho cleared _echoit", _echoit, 0);
    echo();
    show("echo");
    ck("echo set ECHO", _tty.sg_flags & ECHO, ECHO);

    raw();
    show("raw");
    ck("raw set RAW", _tty.sg_flags & RAW, RAW);
    noraw();
    show("noraw");
    ck("noraw cleared RAW", _tty.sg_flags & RAW, 0);

    nonl();
    show("nonl");
    ck("nonl cleared CRMOD", _tty.sg_flags & CRMOD, 0);
    nl();
    show("nl");
    ck("nl set CRMOD", _tty.sg_flags & CRMOD, CRMOD);

    printf("%-30s 0%o\n", "erasechar", erasechar() & 0377);
    printf("%-30s 0%o\n", "killchar", killchar() & 0377);

    // savetty()/resetty() round trip: savetty re-reads the driver, so what it stores is what
    // the console really holds, not what this program last asked for.
    savetty();
    ck("savetty _res_flg", _res_flg, _tty.sg_flags);
    raw();
    resetty();
    show("after resetty");
    ck("resetty restored", _tty.sg_flags, _res_flg);

    // endwin() calls resetty() again; after it the console must be back to what scopen()
    // left, XTABS included -- which is what makes it safe for the shell to keep running.
    endwin();
    ck("_endwin", _endwin, 1);

    printf("\n--- %d error%s\n", errors, errors == 1 ? "" : "s");
    return errors != 0;
}
