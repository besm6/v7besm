/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// /etc/getty -- open a terminal, read a login name, and become /bin/login.
//
//      getty [ selector ]
//
// One of task 29b's two (../../kernel/TODO.md).  /etc/init forks one of these per enabled
// line of /etc/ttys and execs it as `execl("/etc/getty", "-", "<selector>", 0)' with
// descriptors 0, 1 and 2 already open on the terminal (cmd/init/init.c, dfork()).  This
// program sets the line's modes, prints `login: ', reads a name, sets the modes a user wants
// rather than the ones a name is read under, and execs /bin/login with the name as argv[1].
// It never returns: login replaces it, the shell replaces login, and when the user logs out
// that one process finally exits and init starts a fresh getty here.
//
// THE SPEED TABLE IS THIS MACHINE'S, AND THAT IS THE ONE DELIBERATE DIVERGENCE.  v7's itab[]
// is thirteen entries of PDP-11 baud rates -- B110 through B9600, with parity, delay and
// LCASE bits chosen per terminal, and a `nname' successor to fall to when a name comes back
// garbled at the wrong speed.  None of that describes a Consul-254.  It is a PARALLEL,
// character-at-a-time typewriter: there is no baud rate to guess and nothing to fall to.
// Concretely, in this kernel:
//
//   - sg_ispeed/sg_ospeed are inert.  ttioccomm() (kernel/dev/tty.c) stores them and hands
//     them back and NOTHING between reads them, so v7's `case '3'' speed probe --
//     ioctl(TIOCGETP) and a test against B300 -- reads back B0 on every line, always.  It is
//     gone with the speeds themselves.
//
//   - the delay bits describe nothing.  ttyoutput() no longer generates delays at all: the
//     terminal path is eight bits wide, so a queued byte above 0177 is data and cannot also
//     be a delay count (kernel/dev/tty.c).  Nothing was lost -- there is no carriage on this
//     machine's terminal to wait for.
//
//   - parity is not carried.  The Consul line is a byte pipe and all eight bits are the
//     kernel's own (kernel/dev/sc.c), so ANYP/EVENP/ODDP describe nothing.  v7's partab[] --
//     128 bytes of even-parity flags that putchr() ORed into every character it wrote -- is
//     gone for the same reason.
//
//   - LCASE would fold this terminal's lower case away.  The Consul's own code (GOST-10859)
//     has no lower-case Latin at all, which is why dev/sc.c runs the SIMH line `raw8' and
//     speaks ASCII; a getty that turned LCASE on would undo that.
//
// So the table is ONE ENTRY, and it keeps v7's shape rather than collapsing into two
// constants: init still passes a selector character, ttys(5) still defines the column, and a
// second kind of terminal is one line here.  An unknown selector falls back to itab[0], which
// is v7's rule and is now also the only outcome.
//
// The C11 pass is the rest of the diff, and is the mechanical one ../README.md §1 describes:
// prototypes, explicit return types, static on everything but main.  Two things in it are not
// mechanical:
//
//   - §2, THE POINTER COMPARISONS.  getname() bounded its buffer with `np >= &name[16]' and
//     `np > name' over a char *, and a relational operator between two char * gives the wrong
//     answer here -- the byte offset sits above the word address and DECREMENTS as the
//     pointer advances.  Both are an int index now.  This is the bug class that made
//     getpass() return the empty string for months (lib/libc/README.md).
//
//   - v7's local puts() collided with the C11 name.  It is putmsg() here.  Nothing in this
//     program uses stdio -- read() and write() are the whole of its I/O -- and keeping it
//     that way is what makes it small.  _exit() rather than exit() for the same reason: there
//     is nothing buffered to flush and <stdlib.h> need not be opened at all.
//
// ONE UPSTREAM BUG FIXED, and it is the same comparison seen from the other side.  v7 broke
// out of getname()'s loop on `np >= &name[16]' and then wrote `*np = 0' -- so a name of
// exactly sixteen characters stored the terminator at name[16], one past the array.  The
// bound here is NAMESIZE - 1, which leaves the room the terminator needs.
//
// The modes it sets, and why they are these:
//
//   iflags = RAW while the name is read, so that getname() sees the typed CR itself.  In RAW
//     the kernel echoes nothing and translates nothing, so every character on the screen is
//     one putmsg()/putchr() put there and every message here carries its own \r.
//
//   fflags = ECHO|CRMOD|XTABS, which is EXACTLY what scopen() sets on a first open.  A line
//     handed to login is therefore in the state the rest of this system assumes, and CRMOD
//     is not optional: the SIMH line is raw on the far side, so if the kernel does not turn a
//     typed CR into a newline nothing will.
//
// NOT SETUID.  init runs as root and this is its child; there is no privilege to acquire.
//
#include <sgtty.h>
#include <signal.h>
#include <unistd.h>

#define ERASE '#' // what login and the kernel's ttychars() both use
#define KILL  '@'
#define EOT   004 // ^D at the login: prompt means "go away"

// The name buffer.  Sixteen is v7's; login truncates to eight, and getname() below will not
// store past this.
#define NAMESIZE 16

static struct sgttyb tmode;

// The special characters, set once per getty.  TIOCSETC writes the kernel's ONE GLOBAL `tun'
// (kernel/dev/tty.c), not a per-line copy -- v7's arrangement, carried faithfully -- so this
// is a system-wide statement and not a property of this terminal.  The values are the ones
// ttychars() already installs; setting them again costs one syscall and means a getty started
// after something else has moved them puts them back.
static struct tchars tchars = { '\177', '\034', '\021', '\023', '\004', '\377' };

// One kind of terminal: what to select it by, what to leave the line as while the name is
// read, what to leave it as for login, and what to print.  `nname' is the entry to fall to
// when the name comes back unusable, which with one entry is itself.
struct tab {
    char tname;          // this table's selector, as it appears in /etc/ttys column 2
    char nname;          // successor selector, tried when getname() fails
    int iflags;          // modes while the name is read
    int fflags;          // modes handed on to login
    const char *message; // the login prompt
};

static const struct tab itab[] = {
    // '0' -- the Consul-254, and ttys(5) says '0' is what a normal line carries.
    { '0', '0', RAW, ECHO | CRMOD | XTABS, "\r\nlogin: " },
};

#define NITAB (int)(sizeof itab / sizeof itab[0])

static char name[NAMESIZE];
static int crmod; // the name ended with a CR, so the user's terminal wants CRMOD
static int upper; // the name had capitals and no lower case: an upper-case-only terminal
static int lower;

static int getname(void);
static void putmsg(const char *s);
static void putchr(int cc);

int main(int argc, char **argv)
{
    const struct tab *tabp;
    int i, tname;

    tname = '0';
    if (argc > 1)
        tname = argv[1][0];

    for (;;) {
        // Find the selector's entry, or fall back to the first -- v7's rule, and with one
        // entry it is the only outcome.
        tabp = &itab[0];
        for (i = 0; i < NITAB; i++)
            if (itab[i].tname == tname) {
                tabp = &itab[i];
                break;
            }

        tmode.sg_flags = tabp->iflags;
        ioctl(0, TIOCSETP, (char *)&tmode);
        ioctl(0, TIOCSETC, (char *)&tchars);
        putmsg(tabp->message);

        if (getname()) {
            tmode.sg_erase = ERASE;
            tmode.sg_kill  = KILL;
            tmode.sg_flags = tabp->fflags;
            if (crmod)
                tmode.sg_flags |= CRMOD;
            if (upper)
                tmode.sg_flags |= LCASE;
            if (lower)
                tmode.sg_flags &= ~LCASE;
            stty(0, &tmode);
            putchr('\n');
            execl("/bin/login", "login", name, (char *)0);
            _exit(1); // no /bin/login; init will try again
        }
        tname = tabp->nname;
    }
}

//
// Read one login name into name[], echoing it as it comes.  Returns 1 when there is a name to
// hand to login and 0 when the line should be set up again from the top.
//
// The line is RAW here, so nothing is echoed and nothing is translated by the kernel: erase,
// kill and the end of the line are this loop's business, and so is every character that
// appears on the terminal.
//
static int getname(void)
{
    int n, c;
    char cs;

    crmod = 0;
    upper = 0;
    lower = 0;
    n     = 0;
    for (;;) {
        if (read(0, &cs, 1) <= 0)
            _exit(0); // the line went away
        // Eight bits: the line is a byte pipe (kernel/dev/sc.c) and 0177 would cut a
        // UTF-8 character in half.  Everything tested below is ASCII.
        c = cs & 0377;
        if (c == 0)
            return (0); // a null: try the next table
        if (c == EOT)
            _exit(1);
        // §2: n is an int index.  `np >= &name[16]' was a relational between two char *,
        // which does not order them on this machine.
        if (c == '\r' || c == '\n' || n >= NAMESIZE - 1)
            break;
        putchr(cs);
        if (c >= 'a' && c <= 'z') {
            lower++;
        } else if (c >= 'A' && c <= 'Z') {
            upper++;
            c += 'a' - 'A';
        } else if (c == ERASE) {
            if (n > 0)
                n--;
            continue;
        } else if (c == KILL) {
            putchr('\r');
            putchr('\n');
            n = 0;
            continue;
        } else if (c == ' ') {
            c = '_';
        }
        name[n++] = c;
    }
    name[n] = 0;
    if (c == '\r')
        crmod++;
    return (1);
}

static void putmsg(const char *s)
{
    while (*s)
        putchr(*s++);
}

//
// One character to the terminal.  v7 ORed in partab[]'s even-parity bit here; this machine
// carries no parity -- the line is eight bits of data (kernel/dev/sc.c) -- so the byte goes
// out as it stands.
//
static void putchr(int cc)
{
    char c = cc;

    write(1, &c, 1);
}
