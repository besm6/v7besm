// more.c - General purpose tty output filter and file perusal program
//
// by Eric Shienbrood, UC Berkeley
//
// modified by Geoff Peck, UCB to add underlining, single spacing
// modified by John Foderaro, UCB to add -c and MORE environment variable
//
// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.
//
// Task C27; see README.md for what the port to this machine changed and more.1.umm
// for what it documents.  Ported from RetroBSD's src/cmd/more/more.c -- there was no
// more(1) in the v7 tree at all, this being Berkeley's, so it is the second program
// under cmd/ that is not a port of a v7 command.  cmd/novi is the first.
//
// ALSO INSTALLED AS /bin/less, a hard link to the same inode (../../root.manifest).
// Nothing here looks at argv[0]: upstream's `page' alias did, and went with it.
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <setjmp.h>
#include <sgtty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <term.h>
#include <unistd.h>

#define Fopen(s, m)   (Currline = 0, file_pos = 0, fopen(s, m))
#define Ftell(f)      file_pos
#define Fseek(f, off) (file_pos = off, fseek(f, off, 0))
#define Getc(f)       (++file_pos, getc(f))
#define Ungetc(c, f)  (--file_pos, ungetc(c, f))

#define MBIT CBREAK

// LINSIZ was 256 and is 512 because a column here is a CHARACTER, not a byte:
// nextline() folds at Mcol-1 characters and a Cyrillic one costs two bytes, a
// four-byte one four.  See README.md; cmd/pr counts bytes, deliberately, and the
// difference between the two programs is that pr's columns are a layout the user
// gave a width for while more's are the physical screen.
#define LINSIZ       512
#define ctrl(letter) (letter & 077)
#define RUBOUT       '\177'
#define ESC          '\033'

struct sgttyb otty, savetty;
long file_pos, file_size;
int fnum, no_intty, no_tty;
int dum_opt, dlines;
int nscroll  = 11; // Number of lines scrolled by 'd'
int fold_opt = 1;  // Fold long lines
int stop_opt = 1;  // Stop after form feeds
int ssp_opt  = 0;  // Suppress white space
int ul_opt   = 1;  // Underline as best we can
int promptlen;
int Currline; // Line we are currently at
int startup = 1;
int firstf  = 1;
int notell  = 1;
int bad_so; // True if overwriting does not turn off standout
int inwait, Pause, errors;
int within; // true within a file, false between files
int hard, dumb, noscroll, hardtabs, clreol;
char **fnames; // The list of file names
int nfiles;    // Number of files left to process
char *shell;   // The name of the shell to use
int shellp;    // A previous shell command exists
// NO FILE-SCOPE ch.  Upstream has two tentative definitions of one, which b6parse
// rejects; it was only ever scratch for number() and colon(), so each has its own now.
// A local may not duplicate a file-scope name here -- which is why the name had to go
// before it could be used, and why readch()/ttyin()/expand() are named as they are.
jmp_buf restore;
char Line[LINSIZ];      // Line buffer
int Lpp = 24;           // lines per page
char *Clear;            // clear screen
char *eraseln;          // erase line
char *Senter, *Sexit;   // enter and exit standout mode
char *ULenter, *ULexit; // enter and exit underline mode
char *chUL;             // underline character
char *chBS;             // backspace character
char *Home;             // go to home
char *EodClr;           // clear rest of screen
int Mcol = 80;          // number of columns
int soglitch;           // terminal has standout mode glitch
int ulglitch;           // terminal has underline mode glitch
int pstate = 0;         // current UL state
struct {
    long chrctr, line;
} context, screen_start;

// What `h' and `?' print.  Upstream's /share/misc/more.help, one command to a line, in
// four groups and two columns instead.
//
// NINETEEN ROWS BY EIGHTY, AND THAT IS THE POINT OF THE LAYOUT: one fputs() and no paging,
// so a text as tall as the screen loses its first rows to the scroll.  Upstream's filled
// 24 of the 24 rows this program assumes.
//
// ONE ARRAY AND NOT AN ARRAY OF POINTERS: a table of char * needs a relocation each
// and would meet the trap that a string literal cannot initialize a char * inside an
// aggregate initializer at all (CLAUDE.md).  Adjacent literals concatenate at
// translation phase 6 and this is one object, about 280 words of const.
static const char helptext[] =
    "\n"
    "                       more -- k is an optional count\n"
    "\n"
    " Moving                              Searching\n"
    "  <space> <pagedown>  a screen on      /text  search forward for text\n"
    "  b ^B    <pageup>    a screen back    n      repeat the last search\n"
    "  d ^D                half a screen    '      back where the search began\n"
    "  u                   half back\n"
    "  <return> j <down>   k lines on      Files\n"
    "  k        <up>       k lines back     :n     next file\n"
    "  g        <home>     first line       :p     previous file\n"
    "  G        <end>      last screen      :f     show file name and line\n"
    "  k g / k G           line k\n"
    "  s                   skip k lines    Other\n"
    "  f                   skip k screens   =      show current line number\n"
    "  z                   screen; k is     ^L     redraw the screen\n"
    "                      the new size     !cmd   run cmd in a shell\n"
    "                                       .      repeat the last command\n"
    "                                       q Q    quit\n"
    "                                       h ?    this help\n";

void setmode(struct sgttyb *t);
void reset_tty(void);
void clreos(void);
void kill_line(void);
void set_tty(void);
void initterm(void);
void argscan(char *s);
void copy_file(FILE *f);
void doclear(void);
void home(void);
void search(char buf[], FILE *file, int n);
void skiplns(int n, FILE *f);
void screen(FILE *f, int num_lines);
FILE *checkf(char *fs, int *clearfirst);
int command(char *filename, FILE *f);
void erase(int col);
void cleareol(void);
int pr(char *s1);
int nextline(FILE *f, int *length);
void prompt(char *filename);
void prbuf(char *s, int n);
int number(int *cmd);
int colon(char *filename, int cmd, int nlines);
void ttyin(char buf[], int nmax, char pchar);
void do_shell(char *filename);
void error(char *mess);
void execute(char *filename, char *cmd, char *const argv[]);
int readch(void);
int readkey(void);
void skipf(int nskip);
int expand(char *outbuf, int outsize, char *inbuf);
void rdline(FILE *f);
void show(int c);

// The terminal.
//
// RetroBSD replaced 4BSD's termcap calls with five stubs answering hard-coded ANSI --
// li=25, co=80, am, and six literal escape sequences -- and this port kept them while
// b6_prog() had no way to name a third archive.  It has one now (the LIBS keyword,
// ../../scripts/BesmCross.cmake), so this is lib/libtermcap and the /etc/termcap that
// etc/ stages, and more(1) drives whatever terminal the database describes.
//
// THE ENTRY BUFFER IS AT FILE SCOPE, not in initterm().  tgetnum/tgetflag/tgetstr read
// the pointer tgetent() retained, so it must outlive the call; and 1024 chars is 171
// words of a 4096-word stack, which is what the stubbed port deleted these buffers to
// avoid.  tcarea is tgetstr()'s decoding arena -- the sixteen capabilities below come
// to well under half of it, where lib/libcurses/cr_tty.c sizes its _tspace 1024.
//
// 1024 IS SPELLED OUT, not taken from a macro: TBUFSIZ is private to
// lib/libtermcap/termcap.c and <term.h> gives the number in prose only.  It cannot move
// to the header either -- b6cpp rejects a redefinition whose replacement text is not
// character-identical, and termcap.c's own #define is spaced differently (CLAUDE.md).
// lib/libcurses/cr_tty.c writes its genbuf[1024] for the same reason.
static char termbuf[1024];
static char tcarea[512];
static char *tcap = tcarea;

// tputs() takes an output FUNCTION, and putchar() here is the <stdio.h> macro that this
// port went out of its way to keep: `#undef putchar' would turn the two hundred calls in
// this file into an out-of-line call each.  So the macro is wrapped once, here, and
// tput() is the guard every call site used to get from the stub (which was handed only
// capabilities that existed).  affcnt is 1 throughout: nothing here writes a capability
// whose padding scales with the number of lines affected, and this tputs() does not pad
// anyway (lib/libtermcap/README.md -- the Consul-254 cannot be overrun).
static int putch(int c)
{
    return putchar(c);
}

static void tput(char *s)
{
    if (s != NULL && *s != '\0')
        tputs(s, 1, putch);
}

// The six keys that are not one byte.  The codes are above 0377 so that nothing collides
// with a keystroke: readch() answers an unsigned char, and a Cyrillic byte is a value in
// 128..255 (see the note there).  Everything that carries a command character is an int
// for this reason -- command()'s comchar, number()'s argument, lastcmd.
#define K_UP   0400
#define K_DOWN 0401
#define K_PGUP 0402
#define K_PGDN 0403
#define K_HOME 0404
#define K_END  0405

// THE TABLE IS THE DATABASE PLUS A BUILT-IN FALLBACK, and the fallback is not
// belt-and-braces.  more(1) never emits `ks', so a terminal stays in NORMAL cursor mode
// and an xterm sends \E[A, \E[H, \E[F -- while the xterm entry in /etc/termcap lists the
// APPLICATION-mode \EOA, \EOH, \EOF that `ks' would have switched it to.  Sending ks was
// the alternative and was rejected: there is no way to guarantee the matching `ke' (a
// signal this program does not catch leaves the keypad switched), where an extra table
// row costs two words.  The vt100 entry, separately, has no kP, kN, kh or @7 at all.
//
// First match wins, so a capability that duplicates a fallback is stored once.
#define NKEYS 24
static struct {
    char *seq;
    int code;
} keytab[NKEYS];
static int nkeys;

static void addkey(char *seq, int code);
static void backto(FILE *f, int line);
static void backtoend(FILE *f);

// Come here if a quit signal is received
void onquit(int sig)
{
    signal(SIGQUIT, SIG_IGN);
    if (!inwait) {
        putchar('\n');
        if (!startup) {
            signal(SIGQUIT, onquit);
            longjmp(restore, 1);
        } else
            Pause++;
    } else if (!dum_opt && notell) {
        write(2, "[Use q or Q to quit]", 20);
        promptlen += 20;
        notell = 0;
    }
    signal(SIGQUIT, onquit);
}

// THERE IS NO chgwinsz() HERE.  It answered SIGWINCH by re-reading TIOCGWINSZ, and
// this kernel has neither: <sys/signal.h> is v7's fifteen numbers, with no 28, and
// <sys/ttyio.h> has no TIOCGWINSZ and no `struct winsize' -- lib/libcurses and
// cmd/novi/terminal.c both deleted the same path rather than ifdef it away.  The
// screen size is read once, from $LINES and $COLUMNS over the database, and cannot
// change under us.  NOTHING ON THIS IMAGE SETS ANY OF THE THREE: login(1) exports HOME
// and PATH and there is no /etc/profile, so $TERM is unset, initterm() falls back to
// the xterm entry, and an unexported window of any other size is one more(1) will
// misjudge.  The manual page says so and says what to export.
//
// NOR onsusp().  Job control arrived in 4.1BSD: there is no SIGTSTP, no SIGTTOU and
// no sigsetmask() to be had, so `more' cannot be stopped at a prompt and resumed.

// Clean up terminal state and exit. Also come here if interrupt signal received
void end_it(int sig)
{
    reset_tty();
    if (clreol) {
        putchar('\r');
        clreos();
        fflush(stdout);
    } else if (!clreol && (promptlen > 0)) {
        kill_line();
        fflush(stdout);
    } else
        write(2, "\n", 1);
    _exit(0);
}

int main(int argc, char *argv[])
{
    FILE *f;
    char *s;
    char *p;
    int c;
    int left;
    int prnames = 0;
    int initopt = 0;
    int srchopt = 0;
    int clearit = 0;
    int initline;
    char initbuf[80];

    nfiles = argc;
    fnames = argv;
    initterm();
    nscroll = Lpp / 2 - 1;
    if (nscroll <= 0)
        nscroll = 1;
    if ((s = getenv("MORE")))
        argscan(s);
    while (--nfiles > 0) {
        if ((c = (*++fnames)[0]) == '-') {
            argscan(*fnames + 1);
        } else if (c == '+') {
            s = *fnames;
            if (*++s == '/') {
                srchopt++;
                for (++s, p = initbuf; p < initbuf + 79 && *s != '\0';)
                    *p++ = *s++;
                *p = '\0';
            } else {
                initopt++;
                // NOT isdigit(): plain char is unsigned here, so a byte above 0177
                // is a value in 128..255 and <ctype.h>'s table has 129 entries
                // (lib/libc/gen/ctype_.c).  Only isascii() may be applied up there.
                for (initline = 0; *s != '\0'; s++)
                    if (*s >= '0' && *s <= '9')
                        initline = initline * 10 + *s - '0';
                --initline;
            }
        } else
            break;
    }
    // allow clreol only if Home and eraseln and EodClr strings are
    //  defined, and in that case, make sure we are in noscroll mode
    if (clreol) {
        if ((Home == NULL) || (*Home == '\0') || (eraseln == NULL) || (*eraseln == '\0') ||
            (EodClr == NULL) || (*EodClr == '\0'))
            clreol = 0;
        else
            noscroll = 1;
    }
    // Lpp-1, not upstream's Lpp-2 on a scrolling terminal: the prompt row is overwritten
    // by the next screenful's first line, so one row is all the prompt costs.  The line
    // upstream held back was the previous screenful's last, kept as context.
    if (dlines == 0)
        dlines = Lpp - 1;
    left = dlines;
    if (nfiles > 1)
        prnames++;
    if (!no_intty && nfiles == 0) {
        // The name is a literal and not argv[0]: cmd/README.md section 9, and the
        // same reason /bin/less is only a link -- what this program is called does
        // not change what it does, and under b6sim argv[0] is a build path.
        fputs("Usage: more [-cdflpsu] [-lines] [+linenum | +/string] name ...\n", stderr);
        exit(1);
    } else
        f = stdin;
    if (!no_tty) {
        signal(SIGQUIT, onquit);
        signal(SIGINT, end_it);
        setmode(&otty);
    }
    if (no_intty) {
        if (no_tty)
            copy_file(stdin);
        else {
            if ((c = Getc(f)) == '\f')
                doclear();
            else {
                Ungetc(c, f);
                if (noscroll && (c != EOF)) {
                    if (clreol)
                        home();
                    else
                        doclear();
                }
            }
            if (srchopt) {
                search(initbuf, stdin, 1);
                if (noscroll)
                    left--;
            } else if (initopt)
                skiplns(initline, stdin);
            screen(stdin, left);
        }
        no_intty = 0;
        prnames++;
        firstf = 0;
    }

    while (fnum < nfiles) {
        if ((f = checkf(fnames[fnum], &clearit)) != NULL) {
            context.line = context.chrctr = 0;
            Currline                      = 0;
            if (firstf)
                setjmp(restore);
            if (firstf) {
                firstf = 0;
                if (srchopt) {
                    search(initbuf, f, 1);
                    if (noscroll)
                        left--;
                } else if (initopt)
                    skiplns(initline, f);
            } else if (fnum < nfiles && !no_tty) {
                setjmp(restore);
                left = command(fnames[fnum], f);
            }
            if (left != 0) {
                if ((noscroll || clearit) && (file_size != LONG_MAX)) {
                    if (clreol)
                        home();
                    else
                        doclear();
                }
                if (prnames) {
                    if (bad_so)
                        erase(0);
                    if (clreol)
                        cleareol();
                    pr("::::::::::::::");
                    if (promptlen > 14)
                        erase(14);
                    printf("\n");
                    if (clreol)
                        cleareol();
                    printf("%s\n", fnames[fnum]);
                    if (clreol)
                        cleareol();
                    printf("::::::::::::::\n");
                    if (left > Lpp - 4)
                        left = Lpp - 4;
                }
                if (no_tty)
                    copy_file(f);
                else {
                    within++;
                    screen(f, left);
                    within = 0;
                }
            }
            setjmp(restore);
            fflush(stdout);
            fclose(f);
            screen_start.line = screen_start.chrctr = 0L;
            context.line = context.chrctr = 0L;
        }
        fnum++;
        firstf = 0;
    }
    reset_tty();
    exit(0);
}

void argscan(char *s)
{
    for (dlines = 0; *s != '\0'; s++) {
        switch (*s) {
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            dlines = dlines * 10 + *s - '0';
            break;
        case 'd':
            dum_opt = 1;
            break;
        case 'l':
            stop_opt = 0;
            break;
        case 'f':
            fold_opt = 0;
            break;
        case 'p':
            noscroll++;
            break;
        case 'c':
            clreol++;
            break;
        case 's':
            ssp_opt = 1;
            break;
        case 'u':
            ul_opt = 0;
            break;
        }
    }
}

// Open the file named by fs, or return NULL having said why.
//
// UPSTREAM ALSO REFUSED A BINARY FILE and this port does not, because the way it
// recognised one cannot be carried: it read the second byte of the file straight out
// of the stdio buffer -- `switch ((c | *f->_ptr << 8) & 0177777)' -- and compared the
// pair against PDP-11 a.out magic numbers.  Three things are wrong with that here.
// _ptr is not a member any program may reach into; `c' was a char, so a byte above
// 0177 sign-corrupted the or; and this machine's a.out has none of those numbers
// (cross/besm6/b.out.h).  file(1) is on the image for exactly this question, and the
// BUGS section of the manual page says the check is gone.
FILE *checkf(char *fs, int *clearfirst)
{
    struct stat stbuf;
    FILE *f;
    int c;

    if (stat(fs, &stbuf) == -1) {
        fflush(stdout);
        if (clreol)
            cleareol();
        perror(fs);
        return (NULL);
    }
    if ((stbuf.st_mode & S_IFMT) == S_IFDIR) {
        // Ordered against perror() above, which writes descriptor 2 unbuffered
        // while this goes through stdout: without the flush a `2>&1' capture
        // interleaves the two by buffer size rather than by time.
        printf("\n*** %s: directory ***\n\n", fs);
        fflush(stdout);
        return (NULL);
    }
    if ((f = Fopen(fs, "r")) == NULL) {
        fflush(stdout);
        perror(fs);
        return (NULL);
    }
    c = Getc(f);
    if (c == '\f')
        *clearfirst = 1;
    else {
        *clearfirst = 0;
        Ungetc(c, f);
    }
    if ((file_size = stbuf.st_size) == 0)
        file_size = LONG_MAX;
    return (f);
}

// Print out the contents of the file f, one screenful at a time.
//
// Upstream declares a STOP of -10 here and tests `nchars == STOP' below.  nextline()
// answers EOF or a column count and has never answered -10, so both are gone.

void screen(FILE *f, int num_lines)
{
    register int c;
    register int nchars;
    int length;              // length of current line
    static int prev_len = 1; // length of previous line

    for (;;) {
        while (num_lines > 0 && !Pause) {
            if ((nchars = nextline(f, &length)) == EOF) {
                if (clreol)
                    clreos();
                return;
            }
            if (ssp_opt && length == 0 && prev_len == 0)
                continue;
            prev_len = length;
            // PARENTHESISED: && binds tighter than ||, so upstream's
            // `bad_so || (...) && promptlen > 0' does not guard bad_so with the
            // column test at all, and erases a prompt that is not there.
            if ((bad_so || (Senter && *Senter == ' ')) && promptlen > 0)
                erase(0);
            // must clear before drawing line since tabs on some terminals
            // do not erase what they tab over.
            if (clreol)
                cleareol();
            prbuf(Line, length);
            if (nchars < promptlen)
                erase(nchars); // erase () sets promptlen to 0
            else
                promptlen = 0;
            // ALWAYS.  Upstream omits the newline when a line reached Mcol and lets the
            // terminal's automargin wrap instead -- which is right only when Mcol is the
            // real width.  It is a guess here (see initterm), and when it is wrong the
            // cursor stays put and --More-- lands at the end of the line.  nextline()
            // now folds one column early, so nothing is ever pending in the margin.
            prbuf("\n", 1); // will turn off UL if necessary
            num_lines--;
        }
        if (pstate) {
            tput(ULexit);
            pstate = 0;
        }
        fflush(stdout);
        if ((c = Getc(f)) == EOF) {
            if (clreol)
                clreos();
            return;
        }

        if (Pause && clreol)
            clreos();
        Ungetc(c, f);
        setjmp(restore);
        Pause   = 0;
        startup = 0;
        if ((num_lines = command(NULL, f)) == 0)
            return;
        if (hard && promptlen > 0)
            erase(0);
        if (noscroll && num_lines >= dlines) {
            if (clreol)
                home();
            else
                doclear();
        }
        screen_start.line   = Currline;
        screen_start.chrctr = Ftell(f);
    }
}

void copy_file(FILE *f)
{
    register int c;

    while ((c = getc(f)) != EOF)
        putchar(c);
}

static char bell = ctrl('G');

// scanstr()/Sprintf(), which rendered an integer into a string, are gone with their
// one caller: the `v' command, which started an editor at the current line.  So is
// tailequ(), which compared the last component of argv[0] against "page" -- upstream
// answers to that name too, with -p implied.  Three reasons it does not here: -p is
// the same thing and is implemented, cmd/README.md section 9 asks a program not to
// read its own name (under b6sim argv[0] is a staged absolute path), and the loop
// walked to path[-1] for a name with no slash in it.  /bin/less is a link and needs
// none of this, being the same program under a second name and nothing more.

void prompt(char *filename)
{
    if (clreol)
        cleareol();
    else if (promptlen > 0)
        kill_line();
    if (!hard) {
        promptlen = 8;
        if (Senter && Sexit) {
            tput(Senter);
            promptlen += (2 * soglitch);
        }
        if (clreol)
            cleareol();
        pr("--More--");
        if (filename != NULL) {
            promptlen += printf("(Next file: %s)", filename);
        } else if (!no_intty) {
            promptlen += printf("(%d%%)", (int)((file_pos * 100) / file_size));
        }
        if (dum_opt) {
            promptlen += pr("[Press space to continue, 'q' to quit.]");
        }
        if (Senter && Sexit)
            tput(Sexit);
        if (clreol)
            clreos();
        fflush(stdout);
    } else {
        // A bell prints nothing, so there is nothing to erase.  Upstream leaves
        // promptlen stale and erase()'s hard arm then emits a newline for it.
        promptlen = 0;
        write(2, &bell, 1);
    }
    inwait++;
}

// Get a logical line.
//
// RENAMED FROM getline(), which is POSIX's.  Nothing in this libc declares it -- ../manview,
// the manual-page renderer this rename was made for, reads its lines with fgets(3) and never
// wanted the name -- but the clash would be found the day something does.
//
// THE INDEX IS AN int AND NOT A char *.  Upstream's loop condition is
// `p < &Line[LINSIZ-1]', a relational between two char * -- which is correct on this
// machine (the compiler lowers it through b$pdiff) but is an out-of-line call, and
// this one runs ONCE PER BYTE OF INPUT.  cmd/README.md section 2 asks for exactly this
// rewrite; the pointer stays for the stores, where it costs nothing.
//
// COLUMN COUNTS CHARACTERS, NOT BYTES -- see the `(c & 0300) != 0200' clause below and
// README.md.  That is what LINSIZ is 512 for.
//
// AND AN ANSI ESCAPE SEQUENCE COUNTS NOTHING, which is the same rule again and arrived for
// the same reason.  ESC is below ' ' and was already free, but the `[1;33m' after it is
// seven ordinary characters and was charged seven columns, so a coloured line folded that
// much early.  Nothing on this image put escapes into a PAGED STREAM until cmd/manview
// (task C25a) -- more's own control output never came back through here -- and man(1) now
// pipes every page through it.  A CSI run is ESC, `[', the parameter and intermediate
// bytes, and one final byte in `@'..`~'; prbuf() below still walks bytes, which is correct,
// since it is emitting and not measuring.
//
// NOT ASSERTED, and it cannot be here: this whole path needs a terminal, and a terminal
// whose output can still be diffed is kernel/test/console, enabled again now that
// kernel/TODO.md task 35 is answered -- where README.md's `screen half' had been waiting.
int nextline(FILE *f, int *length)
{
    int c;
    int i;
    int column;
    static int colflg;
    int csi; // inside an escape sequence, which occupies no column

    i      = 0;
    column = 0;
    csi    = 0;
    c      = Getc(f);
    if (colflg && c == '\n') {
        Currline++;
        c = Getc(f);
    }
    // LINSIZ-2, not LINSIZ-1: the form-feed arm below appends a second byte, so the
    // looser guard lets i reach LINSIZ and the terminator writes off the end.
    while (i < LINSIZ - 2) {
        if (c == EOF) {
            if (i > 0) {
                Line[i] = '\0';
                *length = i;
                return (column);
            }
            *length = i;
            return (EOF);
        }
        if (c == '\n') {
            Currline++;
            break;
        }
        Line[i++] = c;
        if (csi) {
            // ESC `[' opens a CSI run that ends at a final byte; ESC anything-else is a
            // two-character sequence and the second character closes it.  Neither counts.
            if (csi == 1)
                csi = c == '[' ? 2 : 0;
            else if (c >= '@' && c <= '~')
                csi = 0;
        } else if (c == ESC)
            csi = 1;
        else if (c == '\t')
            if (hardtabs && column < promptlen && !hard) {
                if (eraseln && !dumb) {
                    column = 1 + (column | 7);
                    tput(eraseln);
                    promptlen = 0;
                } else {
                    for (--i; column & 7 && i < LINSIZ - 2; column++) {
                        Line[i++] = ' ';
                    }
                    if (column >= promptlen)
                        promptlen = 0;
                }
            } else
                column = 1 + (column | 7);
        else if (c == '\b' && column > 0)
            column--;
        else if (c == '\r')
            column = 0;
        else if (c == '\f' && stop_opt) {
            Line[i - 1] = '^';
            Line[i++]   = 'L';
            column += 2;
            Pause++;
        } else if (c >= ' ' && c != RUBOUT && (c & 0300) != 0200)
            // A COLUMN IS A CHARACTER.  Bytes 0200..0277 are UTF-8 continuation
            // bytes and occupy no column of their own, exactly as cmd/novi's
            // cont() has it -- without this clause a Cyrillic line folds at forty
            // columns on an eighty-column screen and the percentage stays right
            // while the screen is visibly wrong.  /etc/motd opens in Cyrillic.
            //
            // prbuf() below still walks BYTES, which is correct: it is emitting,
            // not measuring.  The `c == EOF' arm upstream has here is unreachable
            // -- the top of the loop already returned on it -- and is gone.
            column++;
        if (column >= Mcol - 1 && fold_opt)
            break;
        c = Getc(f);
    }
    // >=, not upstream's ==: a tab or the ^L expansion can step past the fold without
    // landing on it, and then the physical newline that follows costs a blank row.
    colflg  = column >= Mcol - 1 && fold_opt;
    *length = i;
    Line[i] = 0;
    return (column);
}

// Erase the rest of the prompt, assuming we are starting at column col.
void erase(int col)
{
    if (promptlen == 0)
        return;
    if (hard) {
        putchar('\n');
    } else {
        if (col == 0)
            putchar('\r');
        if (!dumb && eraseln)
            tput(eraseln);
        else
            for (col = promptlen - col; col > 0; col--)
                putchar(' ');
    }
    promptlen = 0;
}

// Erase the current line entirely
void kill_line(void)
{
    erase(0);
    if (!eraseln || dumb)
        putchar('\r');
}

// force clear to end of line
void cleareol(void)
{
    tput(eraseln);
}

void clreos(void)
{
    tput(EodClr);
}

//  Print string and return number of characters
int pr(char *s1)
{
    register char *s;
    register char c;

    for (s = s1; (c = *s++);)
        putchar(c);
    return (s - s1 - 1);
}

// Print a buffer of n characters
void prbuf(char *s, int n)
{
    register char c;    // next output character
    register int state; // next output char's UL state
#define wouldul(s, n) \
    ((n) >= 2 && (((s)[0] == '_' && (s)[1] == '\b') || ((s)[1] == '\b' && (s)[2] == '_')))

    while (--n >= 0)
        if (!ul_opt)
            putchar(*s++);
        else {
            if (*s == ' ' && pstate == 0 && ulglitch && wouldul(s + 1, n - 1)) {
                s++;
                continue;
            }
            if ((state = wouldul(s, n))) {
                c = (*s == '_') ? s[2] : *s;
                n -= 2;
                s += 3;
            } else
                c = *s++;
            if (state != pstate) {
                if (c == ' ' && state == 0 && ulglitch && wouldul(s, n - 1))
                    state = 1;
                else
                    tput(state ? ULenter : ULexit);
            }
            if (c != ' ' || pstate == 0 || state != 0 || ulglitch == 0)
                putchar(c);
            if (state && *chUL) {
                pr(chBS);
                tput(chUL);
            }
            pstate = state;
        }
}

//  Clear the screen
void doclear(void)
{
    if (Clear && !hard) {
        tput(Clear);

        // Put out carriage return so that system doesn't
        // get confused by escape sequences when expanding tabs
        putchar('\r');
        promptlen = 0;
    }
}

// Go to home position
void home(void)
{
    tput(Home);
}

static int lastcmd, lastarg, lastp;
static int lastcolon;
#define SHLINSIZ 132 // a literal: b6lower will not size an array from a sizeof
char shell_line[SHLINSIZ];

// Read a command and do it. A command consists of an optional integer
// argument followed by the command character.  Return the number of lines
// to display in the next screenful.  If there is nothing more to display
// in the current file, zero is returned.
int command(char *filename, FILE *f)
{
    int nlines;
    int retval;
    int c;       // NOT a char: it receives Getc(), and EOF is -1
    int colonch; // an int for comchar's reason: colon() now reads through readkey()
    int done;
    // comchar IS AN int: a command may be one of the six keys, whose codes are above
    // 0377 and do not fit a char.  lastcmd and lastarg already were, so `.' repeats a
    // key with no further change.
    int comchar;
    int hadcount; // g and G mean different things with a count and without
    char cmdbuf[80];

#define ret(val)  \
    retval = val; \
    done++;       \
    break

    done = 0;
    if (!errors)
        prompt(filename);
    else
        errors = 0;
    for (;;) {
        nlines = number(&comchar);
        lastp = colonch = 0;
        if (comchar == '.') { // Repeat last command
            lastp++;
            comchar = lastcmd;
            nlines  = lastarg;
            if (lastcmd == ':')
                colonch = lastcolon;
        }
        lastcmd = comchar;
        lastarg = nlines;
        // j and k are <down> and <up> exactly.  Mapped after the store above, so that
        // `.' repeats the letter as itself.
        if (comchar == 'j')
            comchar = K_DOWN;
        else if (comchar == 'k')
            comchar = K_UP;
        if (comchar == otty.sg_erase) {
            kill_line();
            prompt(filename);
            continue;
        }
        switch (comchar) {
        case ':':
            retval = colon(filename, colonch, nlines);
            if (retval >= 0)
                done++;
            break;
        case 'b':
        case ctrl('B'): {
            register int initline;

            if (no_intty) {
                write(2, &bell, 1);
                return (-1);
            }

            if (nlines == 0)
                nlines++;

            // ONE ROW, AND IT IS CHARGED TO THE COUNT.  Upstream prints three -- a blank,
            // the text, a blank -- and then a whole screenful on top, so its own banner
            // has scrolled off before it can be read.
            putchar('\r');
            erase(0);
            if (clreol)
                cleareol();
            printf("...back %d page", nlines);
            if (nlines > 1)
                pr("s\n");
            else
                pr("\n");

            initline = Currline - dlines * (nlines + 1);
            backto(f, initline);
            ret(dlines > 1 ? dlines - 1 : 1);
        }
        case ' ':
        case 'z':
            if (nlines == 0)
                nlines = dlines;
            else if (comchar == 'z')
                dlines = nlines;
            ret(nlines);
        case 'd':
        case ctrl('D'):
            if (nlines != 0)
                nscroll = nlines;
            ret(nscroll);

        // `u' is `d' backwards and shares its scroll size.  NO ctrl('U'): CKILL is 025
        // (<sys/tty.h>), so number() eats it as the kill character first.
        case 'u':
            if (no_intty) {
                write(2, &bell, 1);
                return (-1);
            }
            if (nlines != 0)
                nscroll = nlines;
            kill_line();
            backto(f, Currline - dlines - nscroll);
            ret(dlines);
        case 'q':
        case 'Q':
            end_it(0);
            // NOTREACHED -- end_it() ends in _exit()
        case 's':
        case 'f':
            if (nlines == 0)
                nlines++;
            if (comchar == 'f')
                nlines *= dlines;
            putchar('\r');
            erase(0);
            if (clreol)
                cleareol();
            printf("...skipping %d line", nlines);
            if (nlines > 1)
                pr("s\n");
            else
                pr("\n");

            while (nlines > 0) {
                while ((c = Getc(f)) != '\n')
                    if (c == EOF) {
                        retval = 0;
                        done++;
                        goto endsw;
                    }
                Currline++;
                nlines--;
            }
            // The banner row above is charged to the count, but never down to zero:
            // command() returning 0 is what screen() reads as end of file.
            ret(dlines > 1 ? dlines - 1 : 1);
        case '\n':
            if (nlines != 0)
                dlines = nlines;
            else
                nlines = 1;
            ret(nlines);

        // The keypad.  Each of the six is an existing command less the part of it that
        // does not belong on a key: PageDown is ` ' without `z''s side effect of making
        // the count the new screen size, Down is <return> without the same, PageUp is
        // `b' without the `...back N pages' banner it prints because a scrolling
        // terminal has no other way to show that the file moved backwards.
        case K_DOWN:
            if (nlines == 0)
                nlines = 1;
            ret(nlines);
        case K_PGDN:
            if (nlines == 0)
                nlines = dlines;
            ret(nlines);

        // The six that move backwards.  All of them re-seek and re-skip, so all of them
        // want a file: on a pipe there is nothing to seek, which is `b''s own rule.
        // The letters take a count where the keys do not -- hence hadcount, since the
        // bump below destroys the distinction.
        case K_UP:
        case K_PGUP:
        case K_HOME:
        case K_END:
        case 'g':
        case 'G':
            if (no_intty) {
                write(2, &bell, 1);
                return (-1);
            }
            hadcount = (nlines != 0);
            if (nlines == 0)
                nlines++;
            kill_line();
            if (comchar == K_UP)
                backto(f, Currline - dlines - nlines);
            else if (comchar == K_PGUP)
                backto(f, Currline - dlines * (nlines + 1));
            else if (comchar == K_HOME)
                backto(f, 0);
            else if (comchar == K_END)
                backtoend(f);
            else if (hadcount)
                backto(f, nlines - 1); // k g and k G: the screen starts at line k
            else if (comchar == 'g')
                backto(f, 0);
            else
                backtoend(f);
            ret(dlines);
        case '\f':
            if (!no_intty) {
                doclear();
                Fseek(f, screen_start.chrctr);
                Currline = screen_start.line;
                ret(dlines);
            } else {
                write(2, &bell, 1);
                break;
            }
        case '\'':
            if (!no_intty) {
                kill_line();
                pr("\n***Back***\n\n");
                Fseek(f, context.chrctr);
                Currline = context.line;
                ret(dlines);
            } else {
                write(2, &bell, 1);
                break;
            }
        case '=':
            kill_line();
            promptlen = printf("%d", Currline);
            fflush(stdout);
            break;
        case 'n':
            lastp++;
        case '/':
            if (nlines == 0)
                nlines++;
            kill_line();
            pr("/");
            promptlen = 1;
            fflush(stdout);
            if (lastp) {
                write(2, "\r", 1);
                search(NULL, f, nlines); // Use previous r.e.
            } else {
                ttyin(cmdbuf, 78, '/');
                write(2, "\r", 1);
                search(cmdbuf, f, nlines);
            }
            ret(dlines > 1 ? dlines - 1 : 1);
        case '!':
            do_shell(filename);
            break;
        case '?':
        case 'h':
            // THE HELP TEXT IS COMPILED IN, not read from /usr/lib/more.help: it is
            // one screenful that only this command prints, and an inode and a staging
            // rule are more than that is worth.  So there is no `Can't open help file'
            // path either -- h cannot fail.  The manual page's FILES section says so.
            if (noscroll)
                doclear();
            fputs(helptext, stdout);
            prompt(filename);
            break;
            // THERE IS NO `v' COMMAND.  Upstream starts vi(1) at the current line;
            // the editor on this image is novi(1), which takes a file name and no
            // +linenum, so the command could only have delivered half of itself.  It
            // is gone from the help text and from the manual page as well as here.
        default:
            if (dum_opt) {
                kill_line();
                if (Senter && Sexit) {
                    tput(Senter);
                    promptlen = pr("[Press 'h' for instructions.]") + (2 * soglitch);
                    tput(Sexit);
                } else
                    promptlen = pr("[Press 'h' for instructions.]");
                fflush(stdout);
            } else
                write(2, &bell, 1);
            break;
        }
        if (done)
            break;
    }
    putchar('\r');
endsw:
    inwait = 0;
    notell++;
    return (retval);
}

// Execute a colon-prefixed command.
// Returns <0 if not a command that should cause
// more of the file to be printed.
int colon(char *filename, int cmd, int nlines)
{
    int ch;

    // readkey() and not upstream's readch(), the one command path that bypassed the
    // matcher: an arrow after `:' left its two trailing bytes to be read as commands.
    if (cmd == 0)
        ch = readkey();
    else
        ch = cmd;
    lastcolon = ch;
    switch (ch) {
    case 'f':
        kill_line();
        if (!no_intty)
            promptlen = printf("\"%s\" line %d", fnames[fnum], Currline);
        else
            promptlen = printf("[Not a file] line %d", Currline);
        fflush(stdout);
        return (-1);
    case 'n':
        if (nlines == 0) {
            if (fnum >= nfiles - 1)
                end_it(0);
            nlines++;
        }
        putchar('\r');
        erase(0);
        skipf(nlines);
        return (0);
    case 'p':
        if (no_intty) {
            write(2, &bell, 1);
            return (-1);
        }
        putchar('\r');
        erase(0);
        if (nlines == 0)
            nlines++;
        skipf(-nlines);
        return (0);
    case '!':
        do_shell(filename);
        return (-1);
    case 'q':
    case 'Q':
        end_it(0);
        // NOTREACHED
    default:
        write(2, &bell, 1);
        return (-1);
    }
}

// Read a decimal number from the terminal. Set cmd to the non-digit which
// terminates the number.
//
// readkey() AND NOT readch(), and *cmd is an int for it: what terminates the number may
// be one of the six keys, whose codes are above 0377.  Neither the digit arm nor the
// kill arm can be reached by one.
int number(int *cmd)
{
    int i;
    int ch;

    i = 0; // upstream also seeds ch here, and the loop overwrites it at once
    for (;;) {
        ch = readkey();
        if (ch >= '0' && ch <= '9')
            i = i * 10 + ch - '0';
        else if (ch == otty.sg_kill)
            i = 0;
        else {
            *cmd = ch;
            break;
        }
    }
    return (i);
}

void do_shell(char *filename)
{
    char cmdbuf[80];

    kill_line();
    pr("!");
    fflush(stdout);
    promptlen = 1;
    if (lastp)
        pr(shell_line);
    else {
        ttyin(cmdbuf, 78, '!');
        if (expand(shell_line, sizeof(shell_line), cmdbuf)) {
            kill_line();
            promptlen = printf("!%s", shell_line);
        }
    }
    fflush(stdout);
    write(2, "\n", 1);
    promptlen = 0;
    shellp    = 1;
    // An automatic aggregate initialised from runtime values, which is correct here
    // and had no precedent before task C9d (CLAUDE.md); cmd/ranlib does the same.
    char *av[4] = { shell, "-c", shell_line, NULL };
    execute(filename, shell, av);
}

// Search for the nth occurrence of the string in buf.
//
// A LITERAL STRING, NOT A REGULAR EXPRESSION.  re_comp/re_exec are not in this libc
// and are not being added (lib/libc/README.md lists them among what was not ported);
// ed(1)'s engine is next door and is seven hundred lines, and whether a second copy
// or a shared one is right is a question cmd/novi asked first and nobody has settled
// -- novi's search is literal for the same reason.
//
// IT IS ALSO THE BETTER OF THE TWO HERE, which is worth saying because it reads like
// a loss.  strstr() compares bytes, so a Cyrillic string typed at the / prompt
// matches; ed's engine indexes a 129-entry ctype table and would not (section 11).
// A null buf means `the last string again', which is what the n command passes.
static char searchpat[80];

void search(char buf[], FILE *file, int n)
{
    long startline = Ftell(file);
    long line1     = startline;
    long line2     = startline;
    long line3     = startline;
    int lncount;
    int saveln;

    context.line = saveln = Currline;
    context.chrctr        = startline;
    lncount               = 0;
    if (buf != NULL) {
        strncpy(searchpat, buf, sizeof(searchpat) - 1);
        searchpat[sizeof(searchpat) - 1] = '\0';
    }
    if (searchpat[0] == '\0')
        error("No previous search string");
    while (!feof(file)) {
        line3 = line2;
        line2 = line1;
        line1 = Ftell(file);
        rdline(file);
        lncount++;
        if (strstr(Line, searchpat) != NULL) {
            if (--n == 0) {
                if (lncount > 3 || (lncount > 1 && no_intty)) {
                    pr("\n");
                    if (clreol)
                        cleareol();
                    pr("...skipping\n");
                }
                if (!no_intty) {
                    Currline -= (lncount >= 3 ? 3 : lncount);
                    Fseek(file, line3);
                    if (noscroll) {
                        if (clreol) {
                            home();
                            cleareol();
                        } else
                            doclear();
                    }
                } else {
                    kill_line();
                    if (noscroll) {
                        if (clreol) {
                            home();
                            cleareol();
                        } else
                            doclear();
                    }
                    pr(Line);
                    putchar('\n');
                }
                break;
            }
        }
    }
    if (feof(file)) {
        if (!no_intty) {
            // clearerr(), not `file->_flag &= ~_IOEOF' -- upstream reaches into the
            // FILE and asks in a comment why fseek does not do this.  It does:
            // lib/libc/stdio/fseek.c clears the flag, so the Fseek two lines down
            // would have been enough.  The call stays for the reader.
            clearerr(file);
            Currline = saveln;
            Fseek(file, startline);
        } else {
            pr("\nPattern not found\n");
            end_it(0);
        }
        error("Pattern not found");
    }
}

// Run a program and wait for it.
//
// THE ARGUMENT VECTOR IS EXPLICIT.  Upstream declares this `(char *filename, char
// *cmd, char *args, ...)' and then calls execv(cmd, &args) -- taking the address of
// the last named parameter and using it as an argv[].  That is a guess about how a
// variadic call lays its arguments out, it is undefined behaviour in C11, and it is
// not how this machine passes arguments at all (doc/Besm6_Calling_Conventions.md).
// The one surviving caller builds the vector itself.
void execute(char *filename, char *cmd, char *const argv[])
{
    int id;
    int n;

    fflush(stdout);
    reset_tty();
    for (n = 10; (id = fork()) < 0 && n > 0; n--)
        sleep(5);
    if (id == 0) {
        if (!isatty(0)) {
            close(0);
            open("/dev/tty", 0);
        }
        execv(cmd, argv);
        write(2, "exec failed\n", 12);
        exit(1);
    }
    if (id > 0) {
        signal(SIGINT, SIG_IGN);
        signal(SIGQUIT, SIG_IGN);
        // wait(2) here takes no argument that matters and there is no waitpid:
        // <sys/wait.h> has only the one call, so this reaps whatever comes back.
        while (wait((int *)0) > 0)
            ;
        signal(SIGINT, end_it);
        signal(SIGQUIT, onquit);
    } else
        write(2, "can't fork\n", 11);
    set_tty();
    pr("------------------------\n");
    prompt(filename);
}

// Skip n lines in the file f
void skiplns(int n, FILE *f)
{
    int c; // NOT a char: plain char is unsigned here, so `c == EOF' would never
           // be true and this loop would never end at the end of the file

    while (n > 0) {
        while ((c = Getc(f)) != '\n')
            if (c == EOF)
                return;
        n--;
        Currline++;
    }
}

// Position the file at the given line, counting from zero.
//
// The only way backwards there is: rewind and count newlines again, since nothing here
// remembers where a line began.  Factored out of `b', whose four lines this was, for the
// four keys that move backwards -- all of them wanting exactly this and the clamp.
// The caller has already checked no_intty; a pipe cannot be rewound.
static void backto(FILE *f, int line)
{
    if (line < 0)
        line = 0;
    Fseek(f, 0L);
    Currline = 0; // skiplns() will make Currline correct
    skiplns(line, f);
}

// Position the file at its last screenful, for <end> and for a bare G.
//
// TWO PASSES, and there is no cheaper way: nothing here indexes lines and file_size is
// bytes.  It is the one command whose cost grows with the file.
static void backtoend(FILE *f)
{
    int total = 0;
    int c;

    backto(f, 0);
    while ((c = Getc(f)) != EOF)
        if (c == '\n')
            total++;
    backto(f, total - dlines);
}

// Skip nskip files in the file list (from the command line). Nskip may be
// negative.
void skipf(int nskip)
{
    if (nskip == 0)
        return;
    if (nskip > 0) {
        if (fnum + nskip > nfiles - 1)
            nskip = nfiles - fnum - 1;
    } else if (within)
        ++fnum;
    fnum += nskip;
    if (fnum < 0)
        fnum = 0;
    // One banner: upstream welds the text to this newline and prints it again below.
    pr("\n");
    if (clreol)
        cleareol();
    pr("...Skipping ");
    pr(nskip > 0 ? "to file " : "back to file ");
    pr(fnames[fnum]);
    pr("\n");
    if (clreol)
        cleareol();
    pr("\n");
    --fnum;
}

// ----------------------------- Terminal I/O -------------------------------

// $LINES or $COLUMNS, if it names a sane number; otherwise the default -- which is now
// the database's li and co, this being the OVERRIDE and no longer the only channel.
//
// THIS IS WHERE TIOCGWINSZ WAS, and it is cmd/novi/terminal.c's fromenv() -- there is
// no window-size ioctl on this kernel and no SIGWINCH to be told of a change, so the
// size is read once and stands.  A terminal resized under a running more(1) is a
// terminal more(1) still believes to be its old size, and there is nothing to be done
// about it here; lib/libcurses/cr_tty.c settles for the same.
static int fromenv(const char *name, int lo, int hi, int dflt)
{
    char *s = getenv(name);
    int n   = 0;

    if (s == NULL)
        return dflt;
    for (; *s >= '0' && *s <= '9'; s++)
        n = n * 10 + *s - '0';
    if (*s != '\0' || n < lo || n > hi)
        return dflt;
    return n;
}

void initterm(void)
{
    char *term;

    // FOUR ioctls ARE GONE FROM HERE, all of them 4BSD's and none of them on this
    // kernel: TIOCLGET and the local modes LCRTERA/LCRTKIL, which only made the erase
    // echo prettier -- the two arms of ttyin() that read them went with it; TIOCGPGRP
    // and the loop that waited to reach the foreground, there being no process groups
    // to be behind; and TIOCGWINSZ, replaced above.
    no_tty = ioctl(fileno(stdout), TIOCGETP, (char *)&otty);
    if (no_tty == 0) {
        // NO $TERM MEANS xterm, and so does a $TERM the database does not have.  The
        // /etc/termcap on this image is BSD's termcap.small: five entries, one of which
        // (xterm-color) dangles on a tc= that is not there, so an unknown name is the
        // ordinary case rather than the exceptional one and falling straight to `dumb'
        // would cost the erase-line, the underlining and the six keys below for no
        // reason.  Only when xterm itself cannot be read -- no /etc/termcap at all --
        // is the terminal really unknown.
        if ((term = getenv("TERM")) == NULL)
            term = "xterm";
        if (tgetent(termbuf, term) != 1 && tgetent(termbuf, "xterm") != 1) {
            dumb++;
            ul_opt = 0;
        } else {
            Lpp  = fromenv("LINES", 2, 200, tgetnum("li"));
            Mcol = fromenv("COLUMNS", 20, 200, tgetnum("co"));
            if ((Lpp <= 0) || tgetflag("hc")) {
                hard++; // Hard copy terminal
                Lpp = 24;
            }
            if (Mcol <= 0)
                Mcol = 80;

            if (!hard && tgetflag("ns"))
                noscroll++;
            // `am' is not read: the fold leaves the last column empty, so margin
            // behaviour cannot be observed either way.
            bad_so  = tgetflag("xs");
            eraseln = tgetstr("ce", &tcap);
            Clear   = tgetstr("cl", &tcap);
            Senter  = tgetstr("so", &tcap);
            Sexit   = tgetstr("se", &tcap);
            if ((soglitch = tgetnum("sg")) < 0)
                soglitch = 0;

            //  Set up for underlining:  some terminals don't need it;
            //  others have start/stop sequences, still others have an
            //  underline char sequence which is assumed to move the
            //  cursor forward one character.  If underline sequence
            //  isn't available, settle for standout sequence.

            if (tgetflag("ul") || tgetflag("os"))
                ul_opt = 0;
            if ((chUL = tgetstr("uc", &tcap)) == NULL)
                chUL = "";
            if (((ULenter = tgetstr("us", &tcap)) == NULL ||
                 (ULexit = tgetstr("ue", &tcap)) == NULL) &&
                !*chUL) {
                if ((ULenter = Senter) == NULL || (ULexit = Sexit) == NULL) {
                    ULenter = "";
                    ULexit  = "";
                } else
                    ulglitch = soglitch;
            } else {
                if ((ulglitch = tgetnum("ug")) < 0)
                    ulglitch = 0;
            }

            // The pad character is gone with tputs()'s padding: this library does not
            // pad at all and defines no PC, so `pc' had nothing to be assigned to.  The
            // cursor-motion fallback for Home is gone with it -- upstream reached for
            // tgoto(cm, 0, 0) when a terminal had no "ho", and every entry here has one.
            Home   = tgetstr("ho", &tcap);
            EodClr = tgetstr("cd", &tcap);
            if ((chBS = tgetstr("bc", &tcap)) == NULL)
                chBS = "\b";

            // The keypad.  Six capabilities, then the built-in forms -- see the table.
            addkey(tgetstr("ku", &tcap), K_UP);
            addkey(tgetstr("kd", &tcap), K_DOWN);
            addkey(tgetstr("kP", &tcap), K_PGUP);
            addkey(tgetstr("kN", &tcap), K_PGDN);
            addkey(tgetstr("kh", &tcap), K_HOME);
            addkey(tgetstr("@7", &tcap), K_END);
            addkey("\33[A", K_UP);
            addkey("\33OA", K_UP);
            addkey("\33[B", K_DOWN);
            addkey("\33OB", K_DOWN);
            addkey("\33[5~", K_PGUP);
            addkey("\33[I", K_PGUP);
            addkey("\33[6~", K_PGDN);
            addkey("\33[G", K_PGDN);
            addkey("\33[1~", K_HOME);
            addkey("\33[H", K_HOME);
            addkey("\33OH", K_HOME);
            addkey("\33[4~", K_END);
            addkey("\33[F", K_END);
            addkey("\33OF", K_END);
        }
        if ((shell = getenv("SHELL")) == NULL)
            shell = "/bin/sh";
    }
    // otty is overwritten three times and savetty takes what the LAST call left,
    // which is upstream's shape and is deliberate: descriptor 2 is the one the
    // commands are read from and put back.  A failing call leaves otty as it found
    // it -- zeroed, being file-scope bss -- so a `more' with no terminal anywhere
    // runs with a zero sg_erase and sg_kill and never reads a command anyway.
    no_intty = ioctl(fileno(stdin), TIOCGETP, (char *)&otty);
    ioctl(fileno(stderr), TIOCGETP, (char *)&otty);
    savetty  = otty;
    hardtabs = !(otty.sg_flags & XTABS);
    if (!no_tty) {
        otty.sg_flags &= ~ECHO;
        otty.sg_flags |= MBIT;
    }
}

// One keystroke off the terminal.
//
// c IS AN unsigned char AND THE RESULT AN int, so that a Cyrillic byte typed at the
// / prompt arrives as a value in 128..255 and not as something negative.  It is not
// called `ch' because that is a file-scope object here, and b6parse rejects a local
// of the same name outright -- `Duplicate variable declaration' -- rather than
// shadowing it.  ttyin() and expand() were renamed for the same reason.
int readch(void)
{
    unsigned char c;

    if (read(2, (char *)&c, 1) <= 0) {
        if (errno != EINTR)
            exit(0);
        else
            c = otty.sg_kill;
    }
    return (c);
}

// One row of the key table.  A capability that is absent, empty, or does not begin with
// ESC is dropped: the matcher below is only ever entered on an ESC, and a key that sends
// a bare control character (kb=^H, kD=\177) is one this program already has a meaning for.
// A sequence already in the table is dropped too, so a capability that agrees with a
// built-in form is stored once and the capability -- being first -- is the one kept.
static void addkey(char *seq, int code)
{
    int i;

    if (seq == NULL || seq[0] != ESC || seq[1] == '\0' || nkeys >= NKEYS)
        return;
    for (i = 0; i < nkeys; i++)
        if (strcmp(keytab[i].seq, seq) == 0)
            return;
    keytab[nkeys].seq  = seq;
    keytab[nkeys].code = code;
    nkeys++;
}

// One keystroke, or one of the six keys that are a sequence.
//
// Everything but ESC is readch()'s answer unchanged.  On ESC the bytes are collected
// until the buffer is some entry's whole sequence (that key), or is no entry's prefix
// (unknown -- ring the bell and start over on a fresh keystroke).
//
// A BARE ESC BLOCKS UNTIL THE NEXT KEY IS TYPED, and cannot do otherwise: distinguishing
// the key from the start of a sequence needs a timeout, and there is no select(2) here
// and no VMIN/VTIME under sgtty.  more(1) has no ESC command, so what it costs is a
// wasted keystroke; the manual page says so.  readch() itself is unchanged, ttyin()
// reading literal text at the / and ! prompts and needing to see raw bytes.
int readkey(void)
{
    char buf[8]; // longer than any sequence in the table, which is at most four
    int c, n, i, len, cands;

    for (;;) {
        if ((c = readch()) != ESC)
            return c;
        buf[0] = ESC;
        for (n = 1; n < 7;) {
            buf[n++] = readch();
            buf[n]   = '\0';
            cands    = 0;
            for (i = 0; i < nkeys; i++) {
                len = strlen(keytab[i].seq);
                if (len == n && strcmp(keytab[i].seq, buf) == 0)
                    return keytab[i].code;
                if (len > n && strncmp(keytab[i].seq, buf, n) == 0)
                    cands++;
            }
            if (cands == 0)
                break;
        }
        write(2, &bell, 1);
    }
}

static char BS    = '\b';
static char CARAT = '^';

// ONE BACKSPACE, AND NO CHOICE OF TWO.  Upstream's other arm rubbed the character out
// with "\b \b" under TIOCLGET's LCRTERA, which this kernel has not got -- and wrote
// sizeof(char *) rather than 3 bytes of it.  Fixed first, then deleted with its flag.
#define ERASEONECHAR write(2, &BS, 1)

void ttyin(char buf[], int nmax, char pchar)
{
    char *sptr;
    int c;
    int slash = 0;
    int maxlen;
    char cbuf;

    sptr   = buf;
    maxlen = 0;
    while (sptr - buf < nmax) {
        if (promptlen > maxlen)
            maxlen = promptlen;
        c = readch();
        if (c == '\\') {
            slash++;
        } else if ((c == otty.sg_erase) && !slash) {
            if (sptr > buf) {
                --promptlen;
                ERASEONECHAR;
                --sptr;
                if ((*sptr < ' ' && *sptr != '\n') || *sptr == RUBOUT) {
                    --promptlen;
                    ERASEONECHAR;
                }
                continue;
            } else {
                if (!eraseln)
                    promptlen = maxlen;
                longjmp(restore, 1);
            }
        } else if ((c == otty.sg_kill) && !slash) {
            if (hard) {
                show(c);
                putchar('\n');
                putchar(pchar);
            } else {
                putchar('\r');
                putchar(pchar);
                // The LCRTKIL arm went with LCRTERA's; promptlen = 1 below was all it
                // left behind anyway.
                if (eraseln)
                    erase(1);
                promptlen = 1;
            }
            sptr = buf;
            fflush(stdout);
            continue;
        }
        if (slash && (c == otty.sg_kill || c == otty.sg_erase)) {
            ERASEONECHAR;
            --sptr;
        }
        if (c != '\\')
            slash = 0;
        *sptr++ = c;
        if ((c < ' ' && c != '\n' && c != ESC) || c == RUBOUT) {
            c += c == RUBOUT ? -0100 : 0100;
            write(2, &CARAT, 1);
            promptlen++;
        }
        cbuf = c;
        if (c != '\n' && c != ESC) {
            write(2, &cbuf, 1);
            promptlen++;
        } else
            break;
    }
    // GUARDED.  Upstream writes `*--sptr = '\0'' unconditionally, and sptr is still
    // buf when nmax is not positive or the first keystroke ended the line -- so it
    // forms a pointer below the base of the array and stores through it.  Section 2:
    // that is undefined here and nothing need fire.
    if (sptr > buf)
        --sptr;
    *sptr = '\0';
    if (!eraseln)
        promptlen = maxlen;
    if (sptr - buf >= nmax - 1)
        error("Line too long");
}

// Substitute % (this file's name) and ! (the previous command) into a shell command.
//
// BOTH HALVES ARE BOUNDED; upstream bounds neither, and either overruns from a 78-byte
// cmdbuf.  temp cannot go: the `!' arm reads shell_line, which is outbuf.
int expand(char *outbuf, int outsize, char *inbuf)
{
    char *instr;
    char *outstr;
    char *s;
    int c;
    char temp[SHLINSIZ];
    char *end   = temp + sizeof(temp) - 1;
    int changed = 0;

    instr  = inbuf;
    outstr = temp;
#define PUTC(ch)                       \
    do {                               \
        if (outstr >= end)             \
            error("Command too long"); \
        *outstr++ = (ch);              \
    } while (0)

    while ((c = *instr++) != '\0')
        switch (c) {
        case '%':
            if (!no_intty) {
                for (s = fnames[fnum]; *s != '\0'; s++)
                    PUTC(*s);
                changed++;
            } else
                PUTC(c);
            break;
        case '!':
            if (!shellp)
                error("No previous command to substitute for");
            for (s = shell_line; *s != '\0'; s++)
                PUTC(*s);
            changed++;
            break;
        case '\\':
            if (*instr == '%' || *instr == '!') {
                PUTC(*instr);
                instr++;
                break;
            }
        default:
            PUTC(c);
        }
#undef PUTC
    *outstr = '\0';
    if (outsize > 0) {
        strncpy(outbuf, temp, outsize - 1);
        outbuf[outsize - 1] = '\0';
    }
    return (changed);
}

// Echo one character in ^X form.
//
// THE `c < ' '' GUARD IS RIGHT AND MUST STAY RIGHT.  Plain char is unsigned here, so
// every byte above 0177 is a large positive value and falls through to be written
// unchanged -- which is what section 11 wants: a Cyrillic byte is data, not a control
// character to be rendered.  On a machine with signed char the same test sends it
// through the ^X arm and prints junk.
void show(int c)
{
    char cbuf;

    if ((c < ' ' && c != '\n' && c != ESC) || c == RUBOUT) {
        c += c == RUBOUT ? -0100 : 0100;
        write(2, &CARAT, 1);
        promptlen++;
    }
    cbuf = c;
    write(2, &cbuf, 1);
    promptlen++;
}

void error(char *mess)
{
    // cleareol() does not zero promptlen where kill_line() does, so upstream's
    // `promptlen +=' accumulates under -c, one message at a time.
    if (clreol) {
        cleareol();
        promptlen = 0;
    } else
        kill_line();
    promptlen = strlen(mess);
    if (Senter && Sexit) {
        tput(Senter);
        pr(mess);
        tput(Sexit);
    } else
        pr(mess);
    fflush(stdout);
    errors++;
    longjmp(restore, 1);
}

// Put the terminal into the mode `t' describes.
//
// TIOCSETN AND NOT TIOCSETP, which is the one that flushes: `the commands take
// effect immediately' means type-ahead typed before the prompt appeared is still
// the answer to it (kernel/dev/tty.c calls wflushtty() on the SETP arm).
//
// Upstream reached this through `#define stty(fd,argp) ioctl(fd,TIOCSETN,argp)',
// which SHADOWS this libc's real stty(2) -- <sgtty.h> declares it, it is a syscall
// here rather than a wrapper, and a file that defines a macro over its name has
// quietly taken the gate away from itself.  So the call is written out.
void setmode(struct sgttyb *t)
{
    if (no_tty)
        return;
    ioctl(fileno(stderr), TIOCSETN, (char *)t);
}

void set_tty(void)
{
    otty.sg_flags |= MBIT;
    otty.sg_flags &= ~ECHO;
    setmode(&otty);
}

void reset_tty(void)
{
    if (pstate) {
        tput(ULexit);
        fflush(stdout);
        pstate = 0;
    }
    // Upstream's two stores into otty.sg_flags are dead here: savetty is what goes to
    // the terminal, and set_tty() re-sets both bits before it uses otty.
    setmode(&savetty);
}

// Read one raw line into Line, for search() to look at.
//
// c IS AN int AND THE INDEX AN int.  As a char the EOF test never fires (plain char
// is unsigned here), and this loop would append up to LINSIZ-1 bytes of 0377 to the
// last line of every file -- which is the line search() then reports.  The index
// replaces a `p - Line' that ran once per byte of the file; section 2.
void rdline(FILE *f)
{
    int c;
    int i = 0;

    while ((c = Getc(f)) != '\n' && c != EOF && i < LINSIZ - 1)
        Line[i++] = c;
    if (c == '\n')
        Currline++;
    Line[i] = '\0';
}
