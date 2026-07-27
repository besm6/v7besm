//
// cursest -- lib/libcurses: windows, the screen-difference engine, and cursor motion.
//
// WHAT IS WORTH PROVING HERE is not that curses paints a screen -- it did that on a VAX in
// 1981 -- but that it paints one on a machine where a `char *' is a fat pointer.  Eleven
// comparisons between two of them had to go (lib/libcurses/README.md says which and why:
// the byte offset sits above the word address and DECREMENTS as the pointer advances, so
// `<' orders them wrongly and in silence).  Every one of the eleven is a buffer bound or an
// end-of-scan test, so a wrong answer does not fault -- it blanks the wrong span, shifts a
// row by the wrong amount, or decides that clearing to end of line is or is not cheaper than
// printing the blanks.  Nothing but performing the operations and reading both the window
// afterwards and the bytes that went to the terminal catches that, which is what this does.
//
// THIS PROGRAM OWNS ITS ENVIRONMENT, for termcapt.c's reason: tgetent() consults $TERMCAP
// before /etc/termcap, so a developer with that variable set would otherwise change what the
// test reads.  `environ' is crt0's and is a plain extern (<unistd.h>), so the test assigns
// its own one-entry vector -- v7's answer to this, before putenv existed.
//
// ARGV[1] IS THE DATABASE, and that is what lets one .expected adjudicate both harnesses.
// Under b6sim it is the source tree's etc/termcap (lib/test/cursest.args, where run-test.sh
// substitutes @srcdir@); under the booted kernel it is /etc/termcap, THE SAME FILE, staged
// onto the image by etc/CMakeLists.txt.  It must be absolute: tgetent treats a $TERMCAP not
// beginning with `/' as an entry rather than a file name.
//
// My_term IS SET, AND THAT IS THE WHOLE REASON THE TWO HARNESSES CAN AGREE.  initscr()'s
// other path calls gettmode(), which derives GT from XTABS and NONL and _pfast from CRMOD --
// and ioctl is an unconditional no-op under b6sim (cmd/sim/syscall.cpp) while the booted
// kernel's console really is ECHO|CRMOD|XTABS (kernel/dev/sc.c).  Those three flags change
// which cursor motion cr_put.c emits, so the same program would produce two different
// streams.  With My_term set, initscr() asks the tty nothing and all three keep their BSS
// zeros.  curstty.c is the other half: it does NOT set My_term, and runs on the image only.
//
// _putchar AND wgetch ARE THIS PROGRAM'S.  b6ld pulls an archive member only for a symbol
// still undefined, so defining them here means libcurses' putchar.o and getch.o are never
// pulled and the library calls these instead.  _putchar renders every byte printably, so the
// whole cursor-motion stream lands in the expectation as plain ASCII and a failing diff is
// readable rather than a screenful of escapes; nothing in curses counts what it wrote
// (cr_put.c tracks outcol/outline itself), so the rendering cannot perturb the algorithm.
// wgetch feeds canned input, which is the only way to reach the scanw family: the real
// wgetch calls getchar(), and stdin is ctest's under b6sim and the console under the kernel.
//
#include <curses.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int errors;

// The environment this program hands itself; see the header.
static char *envvec[2];
static char tcvar[1024];

// ---------------------------------------------------------------- output rendering

// Rendered columns emitted since the last mark().  Wrapped so that a diff points at a short
// line rather than at a paragraph.
#define WRAP 72
static int outcol;

int _putchar(int c)
{
    char buf[8];
    int n;

    c &= 0377;
    if (c == 0177)
        n = sprintf(buf, "\\177");
    else if (c < 040)
        n = sprintf(buf, "^%c", c + 0100);
    else if (c > 0176)
        n = sprintf(buf, "\\%03o", c);
    else if (c == '\\')
        n = sprintf(buf, "\\\\");
    else
        n = sprintf(buf, "%c", c);
    if (outcol + n > WRAP) {
        printf("\n  ");
        outcol = 2;
    }
    fputs(buf, stdout);
    outcol += n;
    return 0;
}

// Start a new labelled span of terminal output.
static void mark(char *what)
{
    if (outcol)
        printf("\n");
    printf("%s:\n  ", what);
    outcol = 2;
}

// End the current span.
static void endmark(void)
{
    if (outcol)
        printf("\n");
    outcol = 0;
}

// ---------------------------------------------------------------- canned input

static char *inp;

int wgetch(WINDOW *win)
{
    if (inp == 0 || *inp == 0)
        return ERR;
    return *inp++ & 0377;
}

// ---------------------------------------------------------------- checks

static void ck(char *what, int got, int want)
{
    printf("%-30s %6d", what, got);
    if (got != want) {
        printf("  WRONG, wanted %d", want);
        errors++;
    }
    printf("\n");
}

// A window row, rendered with the standout bit shown as an upper-case marker rather than as
// the eighth bit, so the expectation stays plain ASCII.  A cell is seven bits of character
// plus _STANDOUT (curses.h), and reading _y directly is deliberate: winch() masks, and the
// bit is exactly what several of these cases are about.
static void row(WINDOW *win, int y)
{
    int x, c;

    printf("  %2d |", y);
    for (x = 0; x < win->_maxx; x++) {
        c = win->_y[y][x] & 0377;
        if (c & _STANDOUT)
            printf("{%c}", c & 0177);
        else if (c < 040 || c > 0176)
            printf("\\%03o", c);
        else
            printf("%c", c);
    }
    printf("|\n");
}

// A row is interesting if it is not entirely blank.  Dumping only those keeps a 24x80
// screen to a few lines without hiding anything: the count of blank rows is printed too, so
// a row that wrongly became blank still shows up as an arithmetic difference.
static int blankrow(WINDOW *win, int y)
{
    int x;

    for (x = 0; x < win->_maxx; x++)
        if ((win->_y[y][x] & 0177) != ' ')
            return 1;
    return 0;
}

static void dump(char *name, WINDOW *win)
{
    int y, blanks = 0;

    printf("%s: %dx%d at (%d,%d) flags=0%o choff=%d cur=(%d,%d) clear=%d leave=%d scroll=%d\n",
           name, win->_maxy, win->_maxx, win->_begy, win->_begx, win->_flags & 0377,
           win->_ch_off, win->_cury, win->_curx, win->_clear, win->_leave, win->_scroll);
    for (y = 0; y < win->_maxy; y++) {
        if (blankrow(win, y))
            row(win, y);
        else
            blanks++;
    }
    printf("  (%d blank row%s)\n", blanks, blanks == 1 ? "" : "s");
    printf("  firstch:");
    for (y = 0; y < win->_maxy; y++)
        printf(" %d", win->_firstch[y]);
    printf("\n  lastch: ");
    for (y = 0; y < win->_maxy; y++)
        printf(" %d", win->_lastch[y]);
    printf("\n");
}

// ---------------------------------------------------------------- the scenario

//
// Every one of the eleven rewritten sites, each with a check that a wrong `<' would break.
// None of them faults when it is wrong, so only the result tells.
//
static void windows(void)
{
    WINDOW *w, *sub;
    int y, x, n;

    printf("--- windows\n");

    // newwin: every cell of a fresh window must be blank.  That fill was a `char *' scan.
    w = newwin(6, 20, 4, 10);
    ck("newwin != NULL", w != NULL, 1);
    n = 0;
    for (y = 0; y < 6; y++)
        for (x = 0; x < 20; x++)
            if (w->_y[y][x] != ' ')
                n++;
    ck("newwin non-blank cells", n, 0);

    // makenew() must initialise _orig.  A top-level window that came back with garbage
    // there takes delwin()'s and wdeleteln()'s SUBWINDOW path, which frees nothing and
    // copies where it should rotate.  4.3BSD set _orig nowhere at all.
    ck("top-level _orig == NULL", w->_orig == NULL, 1);
    ck("top-level _nextp == self", w->_nextp == w, 1);
    ck("top-level _ch_off", w->_ch_off, 0);

    sub = subwin(w, 3, 8, 5, 12);
    ck("subwin != NULL", sub != NULL, 1);
    ck("sub _orig == parent", sub->_orig == w, 1);
    ck("sub _nextp == parent", sub->_nextp == w, 1);
    ck("parent _nextp == sub", w->_nextp == sub, 1);
    ck("sub _ch_off", sub->_ch_off, sub->_begx - w->_begx);

    // A subwindow does not own its characters: a write through one must show in the other.
    wmove(sub, 0, 0);
    waddstr(sub, "SUB");
    ck("parent sees sub's write", w->_y[1][2] == 'S' && w->_y[1][4] == 'B', 1);

    // wclrtoeol: two `char *' scans became one memset.  Columns before the cursor must
    // survive, columns from it must not.
    wmove(w, 0, 0);
    waddstr(w, "0123456789abcdefghij");
    wmove(w, 0, 8);
    wclrtoeol(w);
    n = 0;
    for (x = 0; x < 8; x++)
        if (w->_y[0][x] != '0' + x)
            n++;
    ck("clrtoeol kept 0..7", n, 0);
    n = 0;
    for (x = 8; x < 20; x++)
        if (w->_y[0][x] != ' ')
            n++;
    ck("clrtoeol blanked 8..19", n, 0);
    dump("after clrtoeol", w);

    // wdelch / winsch: a row shift each way, and the freed cell.
    wmove(w, 2, 0);
    waddstr(w, "ABCDEFGHIJ");
    wmove(w, 2, 0);
    wdelch(w);
    ck("delch shifted left", w->_y[2][0] == 'B' && w->_y[2][8] == 'J', 1);
    ck("delch blanked last col", w->_y[2][19] == ' ', 1);
    wmove(w, 2, 0);
    winsch(w, 'Z');
    ck("insch inserted", w->_y[2][0] == 'Z' && w->_y[2][1] == 'B', 1);
    ck("insch dropped last col", w->_y[2][19] == ' ', 1);

    // wclrtobot: the minx/maxx bookkeeping is live here, unlike in clrtoeol, so the
    // touchline columns are part of the answer.
    wmove(w, 3, 0);
    waddstr(w, "xxxxxxxxxx");
    wmove(w, 2, 4);
    wclrtobot(w);
    dump("after clrtobot", w);

    // wdeleteln / winsertln on a TOP-LEVEL window: row pointers rotate, and the vacated
    // row is blanked by what used to be a `char *' scan.
    wmove(w, 0, 0);
    waddstr(w, "row0");
    wmove(w, 1, 0);
    waddstr(w, "row1");
    wmove(w, 2, 0);
    waddstr(w, "row2");
    wmove(w, 0, 0);
    wdeleteln(w);
    ck("deleteln rotated", w->_y[0][3] == '1' && w->_y[1][3] == '2', 1);
    ck("deleteln blanked last", blankrow(w, 5), 0);
    wmove(w, 0, 0);
    winsertln(w);
    ck("insertln blanked row 0", blankrow(w, 0), 0);
    ck("insertln pushed down", w->_y[1][3] == '1', 1);

    // The same two on a SUBWINDOW, which takes the memmove path instead.
    wmove(sub, 0, 0);
    waddstr(sub, "s0");
    wmove(sub, 1, 0);
    waddstr(sub, "s1");
    wmove(sub, 0, 0);
    wdeleteln(sub);
    ck("sub deleteln moved chars", sub->_y[0][1] == '1', 1);
    ck("sub deleteln blanked last", blankrow(sub, 2), 0);

    // werase: every cell blank, cursor home.
    werase(w);
    n = 0;
    for (y = 0; y < 6; y++)
        n += blankrow(w, y);
    ck("erase left non-blank rows", n, 0);
    ck("erase cury", w->_cury, 0);
    ck("erase curx", w->_curx, 0);

    delwin(w); // takes the subwindow with it
}

//
// overlay and overwrite, and the standout cell that used to go through isspace().
//
static void copies(void)
{
    WINDOW *a, *b;

    printf("--- overlay and overwrite\n");

    a = newwin(4, 10, 0, 0);
    b = newwin(4, 10, 0, 0);
    wmove(a, 1, 0);
    waddstr(a, "AB CD");
    // A standout cell.  isspace() on it read past the end of _ctype_[] -- the table is 129
    // entries and a standout cell is above 0177 -- so whether this character was copied
    // depended on whatever byte followed the table.
    //
    // WHETHER THERE IS ONE DEPENDS ON THE TERMINAL, which is the library being right rather
    // than the test being loose: wstandout() returns NULL and sets no flag when the entry
    // has neither `so' nor `uc', so on the dumb terminal of pass three the cell is an
    // ordinary `S'.  Assert the return value, then assert the cell that follows from it.
    {
        int so = wstandout(a) != NULL;
        ck("wstandout available", so, (SO || UC) ? 1 : 0);
        wmove(a, 1, 6);
        waddch(a, 'S');
        wstandend(a);
        wmove(b, 1, 0);
        waddstr(b, "zzzzzzzzzz");

        overlay(a, b);
        ck("overlay copied non-blank", b->_y[1][0] == 'A' && b->_y[1][1] == 'B', 1);
        ck("overlay kept b under blank", b->_y[1][2] == 'z', 1);
        ck("overlay copied standout", (b->_y[1][6] & 0377) == (so ? ('S' | _STANDOUT) : 'S'),
           1);
    }
    dump("b after overlay", b);

    werase(b);
    wmove(b, 1, 0);
    waddstr(b, "zzzzzzzzzz");
    overwrite(a, b);
    ck("overwrite copied blank too", b->_y[1][2] == ' ', 1);
    dump("b after overwrite", b);

    delwin(a);
    delwin(b);
}

//
// The refresh path.  This is where the two refresh.c comparisons live: the first decides
// where the window line's trailing blanks begin, the second where the screen's do, and
// between them they decide whether emitting CE is cheaper than emitting the blanks.
//
static void painting(void)
{
    printf("--- painting\n");

    mark("initial refresh of stdscr");
    wrefresh(stdscr);
    endmark();

    wmove(stdscr, 2, 0);
    waddstr(stdscr, "the quick brown fox jumps over the lazy dog");
    mark("refresh after one line of text");
    wrefresh(stdscr);
    endmark();

    // One character changed in the middle of a painted line: the diff engine must move
    // there and emit one character, not repaint the line.
    wmove(stdscr, 2, 10);
    waddch(stdscr, 'B');
    mark("refresh after one changed cell");
    wrefresh(stdscr);
    endmark();

    // Now clear the tail of that line.  With CE present this is the case the two rewritten
    // comparisons decide: a correct nlsp/clsp pair emits CE, a wrong one emits the blanks.
    wmove(stdscr, 2, 20);
    wclrtoeol(stdscr);
    mark("refresh after clrtoeol (CE decision)");
    wrefresh(stdscr);
    endmark();

    // Standout, which makech() must bracket with SO/SE.
    wmove(stdscr, 4, 5);
    wstandout(stdscr);
    waddstr(stdscr, "LOUD");
    wstandend(stdscr);
    waddstr(stdscr, "quiet");
    mark("refresh with standout");
    wrefresh(stdscr);
    endmark();

    // A subwindow refresh, which exercises _ch_off in makech()'s firstch/lastch arithmetic.
    {
        WINDOW *w = newwin(3, 12, 8, 20);
        wmove(w, 1, 1);
        waddstr(w, "boxed");
        box(w, '|', '-');
        mark("refresh of a 3x12 window at (8,20)");
        wrefresh(w);
        endmark();
        dump("boxed window", w);
        delwin(w);
    }

    wclear(stdscr);
    mark("refresh after clear");
    wrefresh(stdscr);
    endmark();
}

//
// mvcur over a grid.  No window state at all: this is cr_put.c's fgoto/plod/tabcol alone,
// and it is what separates the CA terminals from the one without.
//
static void motion(void)
{
    static int ys[] = { 0, 5, 23 };
    static int xs[] = { 0, 7, 79 };
    int i, j, k, l;
    char lbl[64];

    printf("--- mvcur grid\n");
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++)
            for (k = 0; k < 3; k++)
                for (l = 0; l < 3; l++) {
                    sprintf(lbl, "mvcur (%d,%d)->(%d,%d)", ys[i], xs[j], ys[k], xs[l]);
                    mark(lbl);
                    mvcur(ys[i], xs[j], ys[k], xs[l]);
                    endmark();
                }
}

//
// printw and scanw.  All four printw entry points get the same arguments and must produce
// the same row: wprintw passed `&args' where its siblings passed `args', which is a bug
// that only shows when the four are compared.  scanw is reached through this program's own
// wgetch (see the header).
//
static void formatted(void)
{
    WINDOW *w;
    int a, b;
    char s[32];

    printf("--- printw and scanw\n");

    w = newwin(6, 40, 0, 0);
    wmove(w, 0, 0);
    ck("wprintw", wprintw(w, "%d %s %c %o %x %%", 42, "str", 'Z', 64, 255), OK);
    wmove(w, 1, 0);
    ck("mvwprintw", mvwprintw(w, 1, 0, "%d %s %c %o %x %%", 42, "str", 'Z', 64, 255), OK);
    ck("wprintw == mvwprintw", memcmp(w->_y[0], w->_y[1], 40), 0);
    dump("printw window", w);

    wmove(stdscr, 0, 0);
    ck("printw", printw("%d %s %c %o %x %%", 42, "str", 'Z', 64, 255), OK);
    ck("mvprintw", mvprintw(1, 0, "%d %s %c %o %x %%", 42, "str", 'Z', 64, 255), OK);
    ck("printw == mvprintw", memcmp(stdscr->_y[0], stdscr->_y[1], 40), 0);

    a = b = 0;
    s[0]  = 0;
    inp   = "42 hello 7\n";
    ck("wscanw count", wscanw(w, "%d %s %d", &a, s, &b), 3);
    ck("wscanw a", a, 42);
    ck("wscanw b", b, 7);
    printf("%-30s %s\n", "wscanw s", s);

    a = b = 0;
    inp   = "13 world 9\n";
    ck("mvwscanw count", mvwscanw(w, 2, 0, "%d %s %d", &a, s, &b), 3);
    ck("mvwscanw a", a, 13);
    ck("mvwscanw b", b, 9);

    // End of input must come back as ERR, not as a character.  `char' is unsigned here, so
    // 4.3BSD's `char inp = getchar()' turned EOF into 0377 and wgetstr never terminated.
    inp = "";
    ck("wgetstr at end of input", wgetstr(w, s), ERR);

    delwin(w);
}

//
// The leaves: the terminal description, and the unctrl table.
//
static void leaves(char *type)
{
    char lname[50], fname[50];
    char genbuf[1024];

    printf("--- descriptions\n");
    // Printed, not asserted: all three differ per terminal by design, and what makes them
    // an assertion is that they appear in the expectation at all.
    printf("%-30s %s\n", "ttytype", ttytype);
    printf("%-30s %d\n", "CA", CA);
    printf("%-30s %d\n", "LINES", LINES);
    printf("%-30s %d\n", "COLS", COLS);

    // setterm() used to write longname()'s answer back through its own argument, which on
    // the My_term path is Def_term -- a string literal in the const pool.  Calling it twice
    // and comparing is what catches a clobber: the second answer would be the first one's
    // output rather than the entry's.
    {
        char first[50];
        strncpy(first, ttytype, sizeof first - 1);
        first[sizeof first - 1] = 0;
        setterm(type);
        ck("ttytype stable across setterm", strcmp(first, ttytype), 0);
    }

    if (tgetent(genbuf, type) == 1) {
        printf("%-30s %s\n", "longname", longname(genbuf, lname));
        printf("%-30s %s\n", "fullname", fullname(genbuf, fname));
    }
}

static void unctrls(void)
{
    int c;

    printf("--- unctrl\n");
    for (c = 0; c < 0200; c += 16) {
        int i;
        printf("  %03o:", c);
        for (i = 0; i < 16; i++)
            printf(" %s", unctrl(c + i));
        printf("\n");
    }
    // The masking is the point: a window cell carries _STANDOUT, so the argument is
    // routinely above 0177 and an unmasked subscript would leave the table.
    printf("%-30s %s\n", "unctrl('A'|_STANDOUT)", unctrl('A' | _STANDOUT));
}

//
// One terminal, start to finish.
//
static void pass(char *type)
{
    printf("\n================ terminal %s ================\n", type);

    // setterm() fills LINES and COLS only when they are ZERO, so a second initscr() against
    // a different terminal would silently keep the first one's geometry.
    LINES = COLS = 0;
    My_term      = TRUE;
    Def_term     = type;

    mark("initscr");
    ck("initscr != NULL", initscr() != NULL, 1);
    endmark();

    dump("stdscr", stdscr);
    printf("curscr: %dx%d at (%d,%d) flags=0%o clear=%d\n", curscr->_maxy, curscr->_maxx,
           curscr->_begy, curscr->_begx, curscr->_flags & 0377, curscr->_clear);

    leaves(type);
    windows();
    copies();
    formatted();
    painting();
    motion();

    mark("endwin");
    endwin();
    endmark();
    ck("_endwin", _endwin, TRUE);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: cursest /path/to/termcap\n");
        return 1;
    }
    strcpy(tcvar, "TERMCAP=");
    strncat(tcvar, argv[1], sizeof(tcvar) - 10);
    envvec[0] = tcvar;
    envvec[1] = 0;
    environ   = envvec;

    unctrls();

    // vt100: cursor addressing (cm), clear-to-end-of-line (ce), standout, auto-margin and
    // the xn glitch.  This is the terminal that reaches both refresh.c comparisons.
    pass("vt100");

    // cons25: a different cm shape, 25 lines rather than 24, al/dl present where vt100 has
    // only the parameterised forms -- so _swflags_ and the line arithmetic differ.
    pass("cons25");

    // A terminal the database does not describe.  tgetent fails, setterm falls back to
    // "xx|dumb:", every capability is NULL and CA is FALSE -- which is the ONLY way into
    // plod()'s no-cursor-addressing branch and fgoto()'s scroll-by-linefeed loop.
    pass("notaterminal");

    printf("\n--- %d error%s\n", errors, errors == 1 ? "" : "s");
    return errors != 0;
}
