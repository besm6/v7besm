//
// Terminal parameters: the guest's sgttyb/tchars against the host's termios.
// tty.h says why this is a file of its own; this one is the translation and the
// promise to put the terminal back.
//
#include "tty.h"

#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <iterator>
#include <map>

//
// ---------------------------------------------------------------------------
// Speeds
// ---------------------------------------------------------------------------
//
// v7's sg_ispeed/sg_ospeed are INDICES -- B0..EXTB, 0..15 (include/sys/ttyio.h) -- not
// baud rates, and the host's B0..B9600 are opaque speed_t values that happen to be small
// integers on some systems and not on others.  So the table is written out rather than
// cast through.
//
// EXTA AND EXTB (14, 15) HAVE NO HOST SPELLING and nothing above B9600 has a v7 one, so
// anything faster reports B9600 -- the fastest thing a v7 program has a name for, and
// the answer that keeps a speed comparison (cmd/getty's `is this line B300?') meaningful
// instead of wrapping into the two extension slots.
//
// A NOTE ON THE OTHER WORLD: the kernel neither reads nor acts on these -- a Consul-254
// has no baud rate, and include/sys/ttyio.h says so -- so TIOCGETP under the booted
// kernel reads back B0 on every line, always, where b6sim reports what the host terminal
// really runs at.  Both are honest about the machine underneath them; a program that
// needs the same answer in both worlds must not ask this question.
//
// The subscript IS the v7 number: SPEEDS[B300] is the host's B300.
static const speed_t SPEEDS[] = {
    B0, B50, B75, B110, B134, B150, B200, B300, B600, B1200, B1800, B2400, B4800, B9600,
};
static const uint8_t V7_B9600 = 13;

static uint8_t host_speed_to_v7(speed_t s)
{
    const speed_t *first = std::begin(SPEEDS), *last = std::end(SPEEDS);
    const speed_t *p = std::find(first, last, s);
    return (p == last) ? V7_B9600 : (uint8_t)(p - first);
}

static speed_t v7_speed_to_host(uint8_t v)
{
    // EXTA and EXTB fall off the end, as does any value a guest invents.
    return (v < std::size(SPEEDS)) ? SPEEDS[v] : B9600;
}

//
// ---------------------------------------------------------------------------
// Modes
// ---------------------------------------------------------------------------
//

//
// XTABS is `expand tabs in the output driver'.  BSD spells the termios bit OXTABS and
// Linux spells it TAB3; neither has both.
//
#if defined(OXTABS)
#define HOST_XTABS OXTABS
#elif defined(TAB3)
#define HOST_XTABS TAB3
#else
#define HOST_XTABS 0
#endif

void termios_to_sgttyb(const struct termios &t, Sgttyb &s)
{
    unsigned f = 0;

    if (t.c_lflag & ECHO)
        f |= SG_ECHO;

    // CRMOD is the pair of mappings, and v7 sets them together.  Either one seen on the
    // host is enough to report it: a terminal with ICRNL and no ONLCR is a state v7
    // cannot describe, and saying CRMOD is nearer than saying nothing.
    if ((t.c_iflag & ICRNL) || (t.c_oflag & ONLCR))
        f |= SG_CRMOD;

    // RAW and CBREAK are both `not canonical'; ISIG is what tells them apart, exactly as
    // kernel/dev/tty.c does -- RAW takes the signal characters as data, CBREAK does not.
    if (!(t.c_lflag & ICANON))
        f |= (t.c_lflag & ISIG) ? SG_CBREAK : SG_RAW;

    if (HOST_XTABS && (t.c_oflag & HOST_XTABS) == HOST_XTABS)
        f |= SG_XTABS;

    if (t.c_iflag & IXOFF)
        f |= SG_TANDEM;

    // LCASE is an upper-case-only terminal, which only Linux still has a bit for.
#ifdef IUCLC
    if (t.c_iflag & IUCLC)
        f |= SG_LCASE;
#endif

    // Parity.  Neither bit set means ANYP in v7 -- both -- which is also what an
    // eight-bit line with no parity generation is, and every line here is one.
    if (t.c_cflag & PARENB)
        f |= (t.c_cflag & PARODD) ? SG_ODDP : SG_EVENP;
    else
        f |= SG_ANYP;

    s.sg_flags  = f;
    s.sg_erase  = (uint8_t)t.c_cc[VERASE];
    s.sg_kill   = (uint8_t)t.c_cc[VKILL];
    s.sg_ispeed = host_speed_to_v7(cfgetispeed(&t));
    s.sg_ospeed = host_speed_to_v7(cfgetospeed(&t));
}

void sgttyb_to_termios(const Sgttyb &s, struct termios &t)
{
    const unsigned f  = s.sg_flags;
    const bool raw    = (f & SG_RAW) != 0;
    const bool cbreak = (f & SG_CBREAK) != 0;

    if (raw) {
        // RAW is every input transformation off and eight bits through, which is what
        // kernel/dev/tty.c's RAW does on the way in -- and, on the way out, what makes
        // ttyoutput() short-circuit into the queue before the tab expansion.  So OPOST
        // goes too: CRMOD and XTABS below are the only things that turn it back on, and
        // RAW outranks both.
        t.c_iflag &= ~(unsigned)(ICRNL | INLCR | IGNCR | ISTRIP | INPCK | PARMRK | BRKINT | IXON);
        t.c_oflag &= ~(unsigned)(OPOST | ONLCR | HOST_XTABS);
        t.c_lflag &= ~(unsigned)(ICANON | ISIG | IEXTEN);
        t.c_cflag     = (t.c_cflag & ~(unsigned)(CSIZE | PARENB)) | CS8;
        t.c_cc[VMIN]  = 1;
        t.c_cc[VTIME] = 0;
    } else if (cbreak) {
        // CBREAK is RAW's input timing with the signal characters still live.
        t.c_lflag &= ~(unsigned)ICANON;
        t.c_lflag |= ISIG;
        t.c_cc[VMIN]  = 1;
        t.c_cc[VTIME] = 0;
    } else {
        t.c_lflag |= ICANON | ISIG;
    }

    if (f & SG_ECHO)
        t.c_lflag |= ECHO;
    else
        t.c_lflag &= ~(unsigned)ECHO;

    if (!raw) {
        if (f & SG_CRMOD) {
            t.c_iflag |= ICRNL;
            t.c_oflag |= ONLCR | OPOST;
        } else {
            t.c_iflag &= ~(unsigned)ICRNL;
            t.c_oflag &= ~(unsigned)ONLCR;
        }
        if (HOST_XTABS) {
            if (f & SG_XTABS)
                t.c_oflag |= HOST_XTABS | OPOST;
            else
                t.c_oflag &= ~(unsigned)HOST_XTABS;
        }
        if (!(f & (SG_CRMOD | SG_XTABS)))
            t.c_oflag &= ~(unsigned)OPOST;

        // Parity, and only outside RAW -- which has just forced eight bits and none.
        switch (f & SG_ANYP) {
        case SG_ODDP:
            t.c_cflag = (t.c_cflag & ~(unsigned)CSIZE) | CS7 | PARENB | PARODD;
            break;
        case SG_EVENP:
            t.c_cflag = ((t.c_cflag & ~(unsigned)(CSIZE | PARODD)) | CS7 | PARENB);
            break;
        default: // ANYP, and `neither', which v7 treats the same way
            t.c_cflag = (t.c_cflag & ~(unsigned)(CSIZE | PARENB)) | CS8;
            break;
        }
    }

    if (f & SG_TANDEM)
        t.c_iflag |= IXOFF;
    else
        t.c_iflag &= ~(unsigned)IXOFF;

#ifdef IUCLC
    if (f & SG_LCASE)
        t.c_iflag |= IUCLC;
    else
        t.c_iflag &= ~(unsigned)IUCLC;
#endif

    t.c_cc[VERASE] = s.sg_erase;
    t.c_cc[VKILL]  = s.sg_kill;
    cfsetispeed(&t, v7_speed_to_host(s.sg_ispeed));
    cfsetospeed(&t, v7_speed_to_host(s.sg_ospeed));
}

//
// ---------------------------------------------------------------------------
// The six special characters
// ---------------------------------------------------------------------------
//
// t_brkc IS THE ODD ONE.  v7's is an extra line delimiter beside newline, and termios
// has no such thing -- VEOL is the nearest and is not the same promise -- so it reports
// 0377, which is v7's own spelling of `no character' (kernel/dev/tty.c compares against
// it, and cmd/getty plants it).  A guest that sets it is not refused; the value is
// simply not something the host line discipline can be asked for.
//
void termios_to_tchars(const struct termios &t, Tchars &c)
{
    c.t_intrc  = (uint8_t)t.c_cc[VINTR];
    c.t_quitc  = (uint8_t)t.c_cc[VQUIT];
    c.t_startc = (uint8_t)t.c_cc[VSTART];
    c.t_stopc  = (uint8_t)t.c_cc[VSTOP];
    c.t_eofc   = (uint8_t)t.c_cc[VEOF];
    c.t_brkc   = 0377;
}

void tchars_to_termios(const Tchars &c, struct termios &t)
{
    t.c_cc[VINTR]  = c.t_intrc;
    t.c_cc[VQUIT]  = c.t_quitc;
    t.c_cc[VSTART] = c.t_startc;
    t.c_cc[VSTOP]  = c.t_stopc;
    t.c_cc[VEOF]   = c.t_eofc;
}

//
// ---------------------------------------------------------------------------
// Putting the terminal back
// ---------------------------------------------------------------------------
//
// stty(2) now really does put the developer's terminal into RAW or CBREAK with ECHO off,
// so b6sim owes it a restore on EVERY way out, and there are three:
//
//   * the guest calls _exit(2), which throws a clean halt that unwinds to main() -- the
//     atexit() below catches this one, and every ordinary error path with it, since
//     main.cpp calls exit() directly in half a dozen places;
//   * b6sim itself dies of a signal.  A guest that has not installed a handler leaves
//     the host disposition at SIG_DFL, so ^C at a `--More--' prompt kills the simulator
//     outright and the atexit never runs.  Hence the handler below;
//   * the guest installs its own handler for that signal, in which case syscall.cpp's
//     ::signal() replaces this one -- and then the guest is the thing that catches it,
//     and leaves through _exit and the atexit.  Covered either way.
//
// The handlers are installed ONLY OVER SIG_DFL, and only for the four that kill a
// process without a core dump.  Taking a signal the guest had already set to SIG_IGN, or
// one syscall.cpp is forwarding, would change what the simulated program sees.
//
static std::map<int, struct termios> saved;
static volatile std::sig_atomic_t restoring;

static void tty_sighandler(int sig)
{
    tty_restore_all();
    ::signal(sig, SIG_DFL);
    ::raise(sig);
}

static void install_once()
{
    static bool done;
    if (done)
        return;
    done = true;

    ::atexit(tty_restore_all);

    static const int FATAL[] = { SIGINT, SIGTERM, SIGHUP, SIGQUIT };
    for (int sig : FATAL) {
        void (*old)(int) = ::signal(sig, tty_sighandler);
        if (old != SIG_DFL)
            ::signal(sig, old); // the guest, or whoever ran b6sim, wants it
    }
}

void tty_remember(int fd)
{
    if (saved.find(fd) != saved.end())
        return;
    struct termios t;
    if (tcgetattr(fd, &t) < 0)
        return; // not a terminal: nothing to put back
    saved[fd] = t;
    install_once();
}

void tty_restore_all()
{
    if (restoring)
        return;
    restoring = 1;
    for (auto &e : saved)
        tcsetattr(e.first, TCSANOW, &e.second);
    saved.clear();
    restoring = 0;
}
