// The terminal half, by Dave W Plummer: raw mode, buffered ANSI output, and an
// escape-sequence decoder.
//
// THERE IS NO termios HERE AND NO ioctl EITHER.  This kernel's terminal interface is
// v7's -- <sgtty.h> over <sys/ttyio.h>, three gates -- and <sys/ioctl.h> does not
// exist as a header at all, so upstream's `#if defined(pdp11)' conditional has one
// live branch and is gone rather than kept as a branch that can never be taken
// (lib/libcurses/cr_tty.c makes the same deletion for the same reason).  RAW and
// CBREAK are both fully implemented in kernel/dev/tty.c; RAW is what this wants.
//
// XTABS NEEDS NO CLEARING.  ttyoutput() short-circuits into the output queue before
// the tab expansion when the line is RAW (kernel/dev/tty.c), so the tabs novi draws
// as spaces are the only tabs the terminal ever sees.
//
// THERE IS NO TIOCGWINSZ.  The console is a Consul-254 typewriter and the kernel has
// no window-size ioctl for anything; 24x80 is the answer libcurses settles for too,
// and $LINES/$COLUMNS are the only other channel -- reading /etc/termcap would mean a
// LIBS keyword in b6_prog() that this program otherwise has no use for.
//
// OUTPUT IS BUFFERED, and that is not a nicety.  Upstream issues one write(2) per
// string, and refresh() emits a cursor address AND a line per row: about fifty
// write(2) calls carrying two thousand characters, per keystroke, through a console
// whose clist blocks are thirty bytes.  One 1 KB buffer makes that two or three.
// term_key() flushes before it blocks, so the screen the user is looking at is
// always the screen novi last drew.
#include "terminal.h"

#include <sgtty.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define OBUF 1024

static struct sgttyb saved_tty;
static int opened;
static char obuf[OBUF];
static int onext;

// 24x80 until term_open() hears otherwise, and safe to read before it runs.
static int trows = 24;
static int tcols = 80;

void term_flush(void)
{
    int done;
    int k;

    done = 0;
    while (done < onext) {
        k = write(1, obuf + done, onext - done);
        if (k <= 0)
            break;
        done += k;
    }
    onext = 0;
}

void term_write(char *s, int n)
{
    int k;

    while (n > 0) {
        if (onext == OBUF)
            term_flush();
        k = OBUF - onext;
        if (k > n)
            k = n;
        (void)memcpy(obuf + onext, s, k);
        onext += k;
        s += k;
        n -= k;
    }
}

void term_put(char *s)
{
    term_write(s, strlen(s));
}

// $LINES and $COLUMNS, or the default.  Read once: term_size() is on refresh()'s
// path and must not be a syscall or a getenv scan.
static int fromenv(char *name, int least, int most, int dflt)
{
    char *s;
    int v;

    s = getenv(name);
    if (s == (char *)0)
        return dflt;
    v = atoi(s);
    if (v < least || v > most)
        return dflt;
    return v;
}

int term_open(void)
{
    struct sgttyb t;

    if (gtty(0, &saved_tty) < 0)
        return -1;
    t = saved_tty;
    t.sg_flags &= ~(ECHO | CRMOD);
    t.sg_flags |= RAW;
    if (stty(0, &t) < 0)
        return -1;
    opened = 1;
    trows  = fromenv("LINES", 3, 200, 24);
    tcols  = fromenv("COLUMNS", 20, 200, 80);
    term_put("\033[?1049h\033[?25l");
    term_flush();
    return 0;
}

void term_close(void)
{
    if (!opened)
        return;
    term_put("\033[?25h\033[0m\033[H\033[J\033[?1049l");
    term_flush();
    (void)stty(0, &saved_tty);
    opened = 0;
}

void term_size(int *rows, int *cols)
{
    *rows = trows;
    *cols = tcols;
}

// \033[<row>;<col>H, without stdio: this runs once per drawn row.
void term_move(int row, int col)
{
    char seq[16];
    int n;

    n        = 0;
    seq[n++] = '\033';
    seq[n++] = '[';
    ++row;
    ++col;
    if (row >= 100)
        seq[n++] = row / 100 + '0';
    if (row >= 10)
        seq[n++] = row / 10 % 10 + '0';
    seq[n++] = row % 10 + '0';
    seq[n++] = ';';
    if (col >= 100)
        seq[n++] = col / 100 + '0';
    if (col >= 10)
        seq[n++] = col / 10 % 10 + '0';
    seq[n++] = col % 10 + '0';
    seq[n++] = 'H';
    term_write(seq, n);
}

void term_clear(void)
{
    term_put("\033[H\033[J");
}

static int readone(void)
{
    unsigned char c;

    if (read(0, (char *)&c, 1) != 1)
        return -1;
    return c;
}

// One key: a byte, or one of the KEY_* codes above 255.
//
// UPSTREAM ALSO ACCEPTED 0233 AS AN EIGHT-BIT CSI INTRODUCER, AND THAT ARM IS GONE.
// 0233 is 0x9B, and 0x9B is the second byte of Cyrillic Л (U+041B, D0 9B) -- and of
// Ы, Ю and a good deal of lower case besides.  With the arm in place, typing Л ate
// the following keystroke as a CSI parameter.  Nothing on this machine emits an
// eight-bit CSI: the Consul line is raw8 and a host terminal sends 7-bit ESC [.
// This is cmd/README.md SS11 -- a program giving a meaning of its own to a byte above
// 0177 -- wearing its input-side face.
int term_key(void)
{
    int a;
    int b;
    int c;
    int final;
    int guard;

    term_flush();
    a = readone();
    if (a != 27)
        return a;
    b = readone();
    if (b != '[' && b != 'O')
        return 27;
    c = readone();
    if (c == 'A')
        return KEY_UP;
    if (c == 'B')
        return KEY_DOWN;
    if (c == 'C')
        return KEY_RIGHT;
    if (c == 'D')
        return KEY_LEFT;
    if (c == 'H')
        return KEY_HOME;
    if (c == 'F')
        return KEY_END;
    if (b == '[' && c == 'I')
        return KEY_PGUP;
    if (b == '[' && c == 'G')
        return KEY_PGDN;
    if (c >= '0' && c <= '9') {
        a = c - '0';
        c = readone();
        while (c >= '0' && c <= '9') {
            a = a * 10 + c - '0';
            c = readone();
        }
        // Skip any sub-parameters.  Bounded: a truncated sequence on a line that
        // delivers no further byte must not spin here.
        final = c;
        for (guard = 0; guard < 8 && final >= 0 && final < 0100; ++guard)
            final = readone();
        if (final == 'A')
            return KEY_UP;
        if (final == 'B')
            return KEY_DOWN;
        if (final == 'C')
            return KEY_RIGHT;
        if (final == 'D')
            return KEY_LEFT;
        if (final == 'H')
            return KEY_HOME;
        if (final == 'F')
            return KEY_END;
        if (final == '~' || final == '^' || final == '$' || final == '@') {
            if (a == 1 || a == 7)
                return KEY_HOME;
            if (a == 2)
                return KEY_INSERT;
            if (a == 3)
                return KEY_DELETE;
            if (a == 4 || a == 8)
                return KEY_END;
            if (a == 5)
                return KEY_PGUP;
            if (a == 6)
                return KEY_PGDN;
        }
    }
    return 27;
}
