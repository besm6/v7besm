// The curses library: windows, a screen image, and the cursor motion that reconciles them.
//
// THIS IS 4.3BSD CURSES, NOT V7'S.  The header that stood here until now was v7's
// `1.7 (4/17/81)', and it was ABI-incompatible with the sources this repo actually had
// waiting in lib/tmp/libcurses/ -- which are Berkeley's later ones.  Five differences, each
// of which corrupts memory rather than merely failing to compile: `struct _win_st' grew
// _ch_off, _nextp and _orig; _SUBWIN went away and every remaining flag bit shifted; `bool'
// was `char' (one byte) where these sources want a whole object; _puts carried a stray
// trailing semicolon that breaks every if/else chain in the library; and the tty-mode macros
// went from stty()/gtty() to ioctl().  The sources won.  See ../lib/libcurses/README.md.
//
// `bool' HERE IS C11's, NOT A MACRO OF OUR OWN.  4.3BSD wrote `#define bool int' and used the
// name for every window flag and every boolean capability; this header includes <stdbool.h>
// and lets the declarations mean the real type.  That works out exactly, because on this
// machine _Bool is a WHOLE WORD like every other scalar -- sizeof(bool) is 6
// (../doc/Besm6_Data_Representation.md) -- so nothing below changes size or offset, and
// `bool *' is an ordinary word pointer, which is what cr_tty.c's table of pointers-to-flags
// needs.  Conversion to bool is a zero test (C11 6.3.1.2), so these objects only ever hold 0
// or 1 and the `if (AM == 0)'-style tests in cr_put.c say what they look like they say.
//
// Keeping BSD's own `#define bool int' would not have worked: a different replacement list
// for a macro <stdbool.h> also defines is a constraint violation (C11 6.10.3p2) in EITHER
// inclusion order, and <stdbool.h> is one of the ten freestanding headers the external
// compiler puts on the default include path.  A program including both would take a
// diagnostic through no fault of its own.  Using the type instead of redefining the name
// avoids that question entirely.  See ../lib/libcurses/README.md.
//
// THE CAPABILITY NAMES ARE GLOBALS, and two of them are libtermcap's business rather than
// this library's.  <term.h> deliberately declares neither `PC' nor `ospeed' -- that tputs
// does no padding, so nothing reads a line speed -- and curses.c/cr_tty.c define them here,
// as 4.xBSD did.  They are written and never read; see ../lib/libtermcap/tputs.c.
//
// A NOTE ON `char' CELLS.  A window cell holds the character in the low seven bits and
// _STANDOUT (0200) in the eighth.  `char' is UNSIGNED on this machine, so that bit survives
// a widening to int -- but it also means a cell is not a valid <ctype.h> subscript, and it
// is why winch() masks.

#ifndef _CURSES_H
#define _CURSES_H

#include <sgtty.h>
#include <stdbool.h>
#include <stdio.h>
#include <term.h>
#include <unctrl.h>

// TRUE and FALSE are the boolean constants; ERR and OK are RETURN CODES and stay int, which
// is also why OK is 1 and ERR is 0 rather than the other way about.
#define TRUE  true
#define FALSE false
#define ERR   (0)
#define OK    (1)

// Window flags.  These are 4.3BSD's values; v7's differed, and mixing the two silently
// mis-reads every window.
#define _ENDLINE   001
#define _FULLWIN   002
#define _SCROLLWIN 004
#define _FLUSH     010
#define _FULLLINE  020
#define _IDLINE    040
#define _STANDOUT  0200
#define _NOCHANGE  -1

// Emit a capability.  NO TRAILING SEMICOLON -- v7's header had one, which turns every
// `if (x) _puts(A); else ...' in the library into a syntax error.
#define _puts(s) tputs(s, 0, _putchar)

typedef struct sgttyb SGTTY;

// Capabilities read out of /etc/termcap by setterm(), in lib/libcurses/cr_tty.c.
//
// `hz' IS NOT AMONG THEM, and its absence is not an oversight.  4.xBSD curses exported the
// Hazeltine boolean as HZ -- and <sys/param.h> has defined HZ as the clock rate since v7, so
// a translation unit naming both headers failed, in whichever order it named them.  Nothing
// in this library ever READ the flag; setterm() only stored it.  So it is private now,
// lib/libcurses/cr_tty.c's static _HZ, which is what that file already does with the `pc'
// capability and _PC.  The capability is still consumed and its table slot still occupied --
// what changed is only that the name is no longer exported.
extern bool AM, BS, CA, DA, DB, EO, HC, IN, MI, MS, NC, NS, OS, UL, XB, XN, XT, XS, XX;
extern char *AL, *BC, *BT, *CD, *CE, *CL, *CM, *CR, *CS, *DC, *DL, *DM, *DO, *ED, *EI, *K0,
    *K1, *K2, *K3, *K4, *K5, *K6, *K7, *K8, *K9, *HO, *IC, *IM, *IP, *KD, *KE, *KH, *KL, *KR,
    *KS, *KU, *LL, *MA, *ND, *NL, *RC, *SC, *SE, *SF, *SO, *SR, *TA, *TE, *TI, *UC, *UE, *UP,
    *US, *VB, *VS, *VE, *AL_PARM, *DL_PARM, *UP_PARM, *DOWN_PARM, *LEFT_PARM, *RIGHT_PARM;

// The pad character.  <term.h> does not declare it -- see the note at the top -- so it is
// this library's, set by setterm() from the `pc' capability and read by nothing.
extern char PC;

// From the tty modes, via gettmode().  `normtty' is 4.xBSD's and is defined in curses.c, but
// nothing here ever assigns it: on Berkeley systems tset(1) was what set it, and there is no
// tset in this tree.  (v7's UPPERCASE is gone -- it was declared and never defined, so any
// program that named it failed to link.)
extern bool GT, NONL, normtty, _pfast;

struct _win_st {
    short _cury, _curx;
    short _maxy, _maxx;
    short _begy, _begx;
    short _flags;
    short _ch_off; // a subwindow's column offset into its parent's _firstch/_lastch
    bool _clear;
    bool _leave;
    bool _scroll;
    char **_y;
    short *_firstch;
    short *_lastch;
    struct _win_st *_nextp, *_orig; // the subwindow ring, and the parent (NULL if top-level)
};

#define WINDOW struct _win_st

extern bool My_term, _echoit, _rawmode, _endwin;

extern char *Def_term, ttytype[];

extern int LINES, COLS, _tty_ch, _res_flg;

extern SGTTY _tty;

extern WINDOW *stdscr, *curscr;

#define VOID(x) ((void)(x))

// Pseudo-functions on the standard screen.
#define addch(ch)   waddch(stdscr, ch)
#define getch()     wgetch(stdscr)
#define addstr(str) VOID(waddstr(stdscr, str))
#define getstr(str) VOID(wgetstr(stdscr, str))
#define move(y, x)  wmove(stdscr, y, x)
#define clear()     VOID(wclear(stdscr))
#define erase()     VOID(werase(stdscr))
#define clrtobot()  VOID(wclrtobot(stdscr))
#define clrtoeol()  VOID(wclrtoeol(stdscr))
#define insertln()  VOID(winsertln(stdscr))
#define deleteln()  VOID(wdeleteln(stdscr))
#define refresh()   VOID(wrefresh(stdscr))
#define inch()      winch(stdscr)
#define insch(c)    VOID(winsch(stdscr, c))
#define delch()     VOID(wdelch(stdscr))
#define standout()  VOID(wstandout(stdscr))
#define standend()  VOID(wstandend(stdscr))

// mv functions.
#define mvwaddch(win, y, x, ch)   (wmove(win, y, x) == ERR ? ERR : waddch(win, ch))
#define mvwgetch(win, y, x)       VOID(wmove(win, y, x) == ERR ? ERR : wgetch(win))
#define mvwaddstr(win, y, x, str) VOID(wmove(win, y, x) == ERR ? ERR : waddstr(win, str))
#define mvwgetstr(win, y, x, str) VOID(wmove(win, y, x) == ERR ? ERR : wgetstr(win, str))
#define mvwinch(win, y, x)        VOID(wmove(win, y, x) == ERR ? ERR : winch(win))
#define mvwdelch(win, y, x)       VOID(wmove(win, y, x) == ERR ? ERR : wdelch(win))
#define mvwinsch(win, y, x, c)    VOID(wmove(win, y, x) == ERR ? ERR : winsch(win, c))
#define mvaddch(y, x, ch)         mvwaddch(stdscr, y, x, ch)
#define mvgetch(y, x)             mvwgetch(stdscr, y, x)
#define mvaddstr(y, x, str)       mvwaddstr(stdscr, y, x, str)
#define mvgetstr(y, x, str)       mvwgetstr(stdscr, y, x, str)
#define mvinch(y, x)              mvwinch(stdscr, y, x)
#define mvdelch(y, x)             mvwdelch(stdscr, y, x)
#define mvinsch(y, x, c)          mvwinsch(stdscr, y, x, c)

// Pseudo-functions on a window.  winch() masks off _STANDOUT: the eighth bit is this
// library's bookkeeping, not part of the character the caller put there.
#define clearok(win, bf)  (win->_clear = bf)
#define leaveok(win, bf)  (win->_leave = bf)
#define scrollok(win, bf) (win->_scroll = bf)
#define flushok(win, bf)  (bf ? (win->_flags |= _FLUSH) : (win->_flags &= ~_FLUSH))
#define getyx(win, y, x)  y = win->_cury, x = win->_curx
#define winch(win)        (win->_y[win->_cury][win->_curx] & 0177)

// The tty modes.  Every one of these is an ioctl(2) on _tty_ch, so under the a.out simulator
// (cmd/sim/syscall.cpp, where ioctl is an unconditional success that does nothing) they
// change _tty's copy of the flags and nothing else; under the booted kernel they reach
// kernel/dev/tty.c's ttioccomm() and really change the console.
#define raw()                                                                                \
    (_tty.sg_flags |= RAW, _pfast = _rawmode = TRUE, ioctl(_tty_ch, TIOCSETP, (char *)&_tty))
#define noraw()                                                                              \
    (_tty.sg_flags &= ~RAW, _rawmode = FALSE, _pfast = !(_tty.sg_flags & CRMOD),             \
     ioctl(_tty_ch, TIOCSETP, (char *)&_tty))
#define cbreak()                                                                             \
    (_tty.sg_flags |= CBREAK, _rawmode = TRUE, ioctl(_tty_ch, TIOCSETP, (char *)&_tty))
#define nocbreak()                                                                           \
    (_tty.sg_flags &= ~CBREAK, _rawmode = FALSE, ioctl(_tty_ch, TIOCSETP, (char *)&_tty))
#define crmode()   cbreak()   // backwards compatibility
#define nocrmode() nocbreak() // backwards compatibility
#define echo()     (_tty.sg_flags |= ECHO, _echoit = TRUE, ioctl(_tty_ch, TIOCSETP, (char *)&_tty))
#define noecho()                                                                             \
    (_tty.sg_flags &= ~ECHO, _echoit = FALSE, ioctl(_tty_ch, TIOCSETP, (char *)&_tty))
#define nl()                                                                                 \
    (_tty.sg_flags |= CRMOD, _pfast = _rawmode, ioctl(_tty_ch, TIOCSETP, (char *)&_tty))
#define nonl()                                                                               \
    (_tty.sg_flags &= ~CRMOD, _pfast = TRUE, ioctl(_tty_ch, TIOCSETP, (char *)&_tty))
#define savetty()                                                                            \
    ((void)ioctl(_tty_ch, TIOCGETP, (char *)&_tty), _res_flg = _tty.sg_flags)
#define resetty()                                                                            \
    (_tty.sg_flags = _res_flg, (void)ioctl(_tty_ch, TIOCSETP, (char *)&_tty))

#define erasechar() (_tty.sg_erase)
#define killchar()  (_tty.sg_kill)
#define baudrate()  (_tty.sg_ospeed)

// Screen and window life cycle.
WINDOW *initscr(void);
WINDOW *newwin(int num_lines, int num_cols, int begy, int begx);
WINDOW *subwin(WINDOW *orig, int num_lines, int num_cols, int begy, int begx);
int delwin(WINDOW *win);
int mvwin(WINDOW *win, int by, int bx);
void endwin(void);

// Terminal description.  longname() and fullname() fill `def', which must hold 50
// characters -- the size of ttytype[], their only caller's buffer.
int setterm(char *type);
char *longname(const char *bp, char *def);
char *fullname(const char *bp, char *def);
char *getcap(char *name);

// Output.
int waddch(WINDOW *win, char c);
int waddstr(WINDOW *win, char *str);
int wmove(WINDOW *win, int y, int x);
int wclear(WINDOW *win);
void werase(WINDOW *win);
void wclrtoeol(WINDOW *win);
void wclrtobot(WINDOW *win);
int wdelch(WINDOW *win);
int winsch(WINDOW *win, char c);
int wdeleteln(WINDOW *win);
void winsertln(WINDOW *win);
int scroll(WINDOW *win);
void box(WINDOW *win, char vert, char hor);
char *wstandout(WINDOW *win);
char *wstandend(WINDOW *win);
void idlok(WINDOW *win, bool bf);

// Input.
int wgetch(WINDOW *win);
int wgetstr(WINDOW *win, char *str);

// Formatted I/O.  These cannot be macros -- the argument count varies.
int printw(char *fmt, ...);
int wprintw(WINDOW *win, char *fmt, ...);
int mvprintw(int y, int x, char *fmt, ...);
int mvwprintw(WINDOW *win, int y, int x, char *fmt, ...);
int scanw(char *fmt, ...);
int wscanw(WINDOW *win, char *fmt, ...);
int mvscanw(int y, int x, char *fmt, ...);
int mvwscanw(WINDOW *win, int y, int x, char *fmt, ...);

// Refresh, and the change bookkeeping it consumes.
int wrefresh(WINDOW *win);
int touchwin(WINDOW *win);
int touchline(WINDOW *win, int y, int sx, int ex);
void touchoverlap(WINDOW *win1, WINDOW *win2);
void overlay(WINDOW *win1, WINDOW *win2);
void overwrite(WINDOW *win1, WINDOW *win2);

// Cursor motion, exported because a program that leaves curses has to be able to park the
// cursor somewhere sensible first.  _putchar is the one-character output routine the whole
// library funnels through, and a program may define its own to capture the stream.
void mvcur(int ly, int lx, int y, int x);
int _putchar(int c);

#endif // _CURSES_H
