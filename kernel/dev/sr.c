/*
 * Serial / terminal driver -- SKELETON.
 *
 * The terminal lines hang off the machine's 24-line multiplexer, reached through the
 * 033 «увв» channel and answering in ПРП/ГРП (doc/Besm6_Peripherals.md).  None of that
 * is written yet: what is here is the tty/line-discipline scaffolding and the public
 * surface that conf.c wires into cdevsw, as stubs, so the kernel builds and links while
 * the real multiplexer driver is written.
 */

// clang-format off
#include "sys/types.h"
#include "sys/param.h"
#include "sys/conf.h"
#include "sys/dir.h"
#include "sys/user.h"
#include "sys/tty.h"
#include "sys/systm.h"
// clang-format on

#define NSR    2
#define SSPEED 13 /* 9600 bps */

/*
 * Speed table (kept: srparam's zero-speed guard indexes it).
 */
short srstab[] = {
    // clang-format off
    0,      /* 0 */
    2304,   /* 50 */
    1536,   /* 75 */
    1047,   /* 110 */
    857,    /* 134.5 */
    768,    /* 150 */
    576,    /* 200 */
    384,    /* 300 */
    192,    /* 600 */
    96,     /* 1200 */
    64,     /* 1800 */
    48,     /* 2400 */
    24,     /* 4800 */
    12,     /* 9600 */
    6,      /* 19200 */
    3       /* 38400 */
    // clang-format on
};

struct tty sr[NSR];

void srstart(struct tty *tp);
void srparam(struct tty *tp);

void sropen(dev_t dev, int flag)
{
    struct tty *tp;
    int d;

    d = minor(dev);
    if (d >= NSR) {
        u.u_error = ENXIO;
        return;
    }
    tp          = &sr[d];
    tp->t_addr  = d; /* line number; the BESM-6 mux address, TBD */
    tp->t_oproc = srstart;
    if ((tp->t_state & ISOPEN) == 0) {
        tp->t_state  = CARR_ON;
        tp->t_ispeed = SSPEED;
        tp->t_ospeed = SSPEED;
        tp->t_flags  = RAW | ODDP | EVENP | ECHO;
        srparam(tp);
        ttychars(tp);
    }
    ttyopen(dev, tp);
}

void srclose(dev_t dev, int flag)
{
    ttyclose(&sr[minor(dev)]);
}

void srread(dev_t dev)
{
    ttread(&sr[minor(dev)]);
}

void srwrite(dev_t dev)
{
    ttwrite(&sr[minor(dev)]);
}

void srintr(dev_t dev)
{
    /* TODO: BESM-6 terminal multiplexer receive/transmit interrupt (ПРП/ГРП). */
}

void srioctl(dev_t dev, int cmd, caddr_t addr, int flag)
{
    struct tty *tp;

    tp = &sr[minor(dev)];
    if (ttioccomm(cmd, tp, addr, dev)) {
        if (cmd == TIOCSETP || cmd == TIOCSETN)
            srparam(tp);
    } else
        u.u_error = ENOTTY;
}

void srstart(struct tty *tp)
{
    int c;

    if (tp->t_state & (TIMEOUT | BUSY | TTSTOP))
        return;
    if ((c = getc(&tp->t_outq)) >= 0) {
        if (c >= 0200 && (tp->t_flags & RAW) == 0) {
            tp->t_state |= TIMEOUT;
            timeout(ttrstrt, (carg_t)tp, (c & 0177) + 6);
        } else {
            tp->t_char = c;
            tp->t_state |= BUSY;
            /* TODO: BESM-6 terminal multiplexer -- transmit `c'. */
        }
        if (tp->t_outq.c_cc <= TTLOWAT && tp->t_state & ASLEEP) {
            tp->t_state &= ~ASLEEP;
            wakeup((chan_t)&tp->t_outq);
        }
    }
}

void srparam(struct tty *tp)
{
    if (srstab[(unsigned)tp->t_ispeed] == 0)
        return;
    /* TODO: BESM-6 terminal multiplexer -- program line speed / framing. */
}
