// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// general TTY subroutines
#include "sys/tty.h"

#include "sys/conf.h"
#include "sys/dir.h"
#include "sys/file.h"
#include "sys/inode.h"
#include "sys/param.h"
#include "sys/proc.h"
#include "sys/reg.h"
#include "sys/signal.h"
#include "sys/systm.h"
#include "sys/types.h"
#include "sys/user.h"

int tk_nin;
int tk_nout;

char canonb[CANBSIZ]; // buffer for erase and kill (#@)

extern char partab[];

// Input mapping table-- if an entry is non-zero, when the
// corresponding character is typed preceded by "\" the escape
// sequence is replaced by the table value.  Mostly used for
// upper-case only terminals.

char maptab[] = {
    // clang-format off
    000, 000, 000, 000, 000, 000, 000, 000,
    000, 000, 000, 000, 000, 000, 000, 000,
    000, 000, 000, 000, 000, 000, 000, 000,
    000, 000, 000, 000, 000, 000, 000, 000,
    000, '|', 000, 000, 000, 000, 000, '`',
    '{', '}', 000, 000, 000, 000, 000, 000,
    000, 000, 000, 000, 000, 000, 000, 000,
    000, 000, 000, 000, 000, 000, 000, 000,
    000, 000, 000, 000, 000, 000, 000, 000,
    000, 000, 000, 000, 000, 000, 000, 000,
    000, 000, 000, 000, 000, 000, 000, 000,
    000, 000, 000, 000, 000, 000, '~', 000,
    000, 'A', 'B', 'C', 'D', 'E', 'F', 'G',
    'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',
    'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W',
    'X', 'Y', 'Z', 000, 000, 000, 000, 000,
    // clang-format on
};

// shorthand
#define q1 tp->t_rawq
#define q2 tp->t_canq
#define q3 tp->t_outq
#define q4 tp->t_un.t_ctlq

void wflushtty(struct tty *tp);
void ttyblock(struct tty *tp);
void ttyoutput(int c, struct tty *tp);

// routine called on first teletype open.
// establishes a process group for distribution
// of quits and interrupts from the tty.
void ttyopen(dev_t dev, register struct tty *tp)
{
    register struct proc *pp;

    pp        = u.u_procp;
    tp->t_dev = dev;
    if (pp->p_pgrp == 0) {
        u.u_ttyp = tp;
        u.u_ttyd = dev;
        if (tp->t_pgrp == 0)
            tp->t_pgrp = pp->p_pid;
        pp->p_pgrp = tp->t_pgrp;
    }
    tp->t_state &= ~WOPEN;
    tp->t_state |= ISOPEN;
}

// set default control characters.
void ttychars(register struct tty *tp)
{
    tun.t_intrc  = CINTR;
    tun.t_quitc  = CQUIT;
    tun.t_startc = CSTART;
    tun.t_stopc  = CSTOP;
    tun.t_eofc   = CEOT;
    tun.t_brkc   = CBRK;
    tp->t_erase  = CERASE;
    tp->t_kill   = CKILL;
}

// clean tp on last close
void ttyclose(register struct tty *tp)
{
    tp->t_pgrp = 0;
    wflushtty(tp);
    tp->t_state = 0;
}

// stty/gtty writearound
void stty()
{
    u.u_arg[2] = u.u_arg[1];
    u.u_arg[1] = TIOCSETP;
    ioctl();
}

void gtty()
{
    u.u_arg[2] = u.u_arg[1];
    u.u_arg[1] = TIOCGETP;
    ioctl();
}

// ioctl system call
// Check legality, execute common code, and switch out to individual
// device routine.
void ioctl()
{
    register struct file *fp;
    register struct inode *ip;
    register struct a {
        int fdes;
        int cmd;
        caddr_t cmarg;
    } *uap;
    register dev_t dev;
    register int fmt;

    uap = (struct a *)u.u_ap;
    if ((fp = getf(uap->fdes)) == NULL)
        return;
    if (uap->cmd == FIOCLEX) {
        u.u_pofile[uap->fdes] |= EXCLOSE;
        return;
    }
    if (uap->cmd == FIONCLEX) {
        u.u_pofile[uap->fdes] &= ~EXCLOSE;
        return;
    }
    ip  = fp->f_inode;
    fmt = ip->i_mode & IFMT;
    if (fmt != IFCHR && fmt != IFMPC) {
        u.u_error = ENOTTY;
        return;
    }
    dev = (dev_t)ip->i_un.i_rdev;
    (*cdevsw[major(dev)].d_ioctl)(dev, uap->cmd, uap->cmarg, fp->f_flag);
}

// Common code for several tty ioctl commands
int ttioccomm(int com, register struct tty *tp, caddr_t addr, dev_t dev)
{
    unsigned t;
    struct sgttyb iocb; // v7 had a second copy of this under the name `struct ttiocb'
    extern int nldisp;

    switch (com) {
    // get discipline number
    case TIOCGETD:
        t = tp->t_line;
        if (copyout((caddr_t)&t, addr, sizeof(t)))
            u.u_error = EFAULT;
        break;

    // set line discipline
    case TIOCSETD:
        if (copyin(addr, (caddr_t)&t, sizeof(t))) {
            u.u_error = EFAULT;
            break;
        }
        if (t >= nldisp) {
            u.u_error = ENXIO;
            break;
        }
        if (tp->t_line)
            (*linesw[(unsigned)tp->t_line].l_close)(tp);
        if (t)
            (*linesw[t].l_open)(dev, tp);
        if (u.u_error == 0)
            tp->t_line = t;
        break;

    // prevent more opens on channel
    case TIOCEXCL:
        tp->t_state |= XCLUDE;
        break;
    case TIOCNXCL:
        tp->t_state &= ~XCLUDE;
        break;

    // Set new parameters
    case TIOCSETP:
        wflushtty(tp);
    case TIOCSETN:
        if (copyin(addr, (caddr_t)&iocb, sizeof(iocb))) {
            u.u_error = EFAULT;
            return (1);
        }
        tp->t_ispeed = iocb.sg_ispeed;
        tp->t_ospeed = iocb.sg_ospeed;
        tp->t_erase  = iocb.sg_erase;
        tp->t_kill   = iocb.sg_kill;
        tp->t_flags  = iocb.sg_flags;
        break;

    // send current parameters to user
    case TIOCGETP:
        iocb.sg_ispeed = tp->t_ispeed;
        iocb.sg_ospeed = tp->t_ospeed;
        iocb.sg_erase  = tp->t_erase;
        iocb.sg_kill   = tp->t_kill;
        iocb.sg_flags  = tp->t_flags;
        if (copyout((caddr_t)&iocb, addr, sizeof(iocb)))
            u.u_error = EFAULT;
        break;

        // Hang up line on last close

    case TIOCHPCL:
        tp->t_state |= HUPCLS;
        break;

    case TIOCFLUSH:
        flushtty(tp);
        break;

    // ioctl entries to line discipline
    case DIOCSETP:
    case DIOCGETP:
        (*linesw[(unsigned)tp->t_line].l_ioctl)(com, tp, addr);
        break;

    // Set and fetch special characters.  Same word either way, but b6cc folds
    // &tp->t_un into the pointer arithmetic and puts &tun through a frame slot.
    case TIOCSETC:
        if (copyin(addr, (caddr_t)&tp->t_un, sizeof(tun)))
            u.u_error = EFAULT;
        break;

    case TIOCGETC:
        if (copyout((caddr_t)&tp->t_un, addr, sizeof(tun)))
            u.u_error = EFAULT;
        break;

    default:
        return (0);
    }
    return (1);
}

// Wait for output to drain, then flush input waiting.
void wflushtty(register struct tty *tp)
{
    spl5();
    while (tp->t_outq.c_cc && tp->t_state & CARR_ON) {
        (*tp->t_oproc)(tp);
        tp->t_state |= ASLEEP;
        sleep((chan_t)&tp->t_outq, TTOPRI);
    }
    flushtty(tp);
    spl0();
}

// flush all TTY queues
void flushtty(register struct tty *tp)
{
    register int s;

    while (getc(&tp->t_canq) >= 0)
        ;
    wakeup((chan_t)&tp->t_rawq);
    wakeup((chan_t)&tp->t_outq);
    s = spl6();
    tp->t_state &= ~TTSTOP;
    (*cdevsw[major(tp->t_dev)].d_stop)(tp);
    while (getc(&tp->t_outq) >= 0)
        ;
    while (getc(&tp->t_rawq) >= 0)
        ;
    tp->t_delct  = 0;
    tp->t_echoct = 0; // the queue is gone, so nothing on the screen is ours to erase
    splx(s);
}

// transfer raw input list to canonical list,
// doing erase-kill processing and handling escapes.
// It waits until a full line has been typed in cooked mode,
// or until any character has been typed in raw mode.
int canon(register struct tty *tp)
{
    register char *bp;
    char *bp1;
    register int c;
    int mc;

    spl5();
    while (((tp->t_flags & (RAW | CBREAK)) == 0 && tp->t_delct == 0) ||
           ((tp->t_flags & (RAW | CBREAK)) != 0 && tp->t_rawq.c_cc == 0)) {
        if ((tp->t_state & CARR_ON) == 0) {
            return (0);
        }
        sleep((chan_t)&tp->t_rawq, TTIPRI);
    }
    spl0();
loop:
    bp = &canonb[2];
    while ((c = getc(&tp->t_rawq)) >= 0) {
        if ((tp->t_flags & (RAW | CBREAK)) == 0) {
            if (c == 0377) {
                tp->t_delct--;
                break;
            }
            if (bp[-1] != '\\') {
                if (c == tp->t_erase) {
                    // THE EDIT IS HERE, THE DISPLAY HALF IS IN ttyinput(): it rubbed this
                    // character off the screen with "\b \b" when it was typed, and the two
                    // halves agree through t_echoct.  Change the back-up rule below and the
                    // counting rule there has to change with it.
                    //
                    // One character, not one byte: back over a UTF-8 sequence's
                    // continuation bytes first.  On ASCII the loop runs zero times.
                    while (bp > &canonb[2] && (bp[-1] & 0300) == 0200)
                        bp--;
                    if (bp > &canonb[2])
                        bp--;
                    continue;
                }
                if (c == tp->t_kill)
                    goto loop;
                if (c == tun.t_eofc)
                    continue;
            } else {
                // maptab[] is 128 entries of ASCII: a UTF-8 byte has none, and is stored
                // as it stands.
                mc = (c < 0200) ? maptab[c] : 0;
                if (c == tp->t_erase || c == tp->t_kill)
                    mc = c;
                if (mc && (mc == c || (tp->t_flags & LCASE))) {
                    if (bp[-2] != '\\')
                        c = mc;
                    bp--;
                }
            }
        }
        *bp++ = c;
        if (bp >= canonb + CANBSIZ)
            break;
    }
    bp1 = &canonb[2];
    b_to_q(bp1, bp - bp1, &tp->t_canq);

    if (tp->t_state & TBLOCK && tp->t_rawq.c_cc < TTYHOG / 5) {
        if (putc(tun.t_startc, &tp->t_outq) == 0) {
            tp->t_state &= ~TBLOCK;
            ttstart(tp);
        }
        tp->t_char = 0;
    }

    return (bp - bp1);
}

// block transfer input handler.
void ttyrend(register struct tty *tp, register char *pb, register char *pe)
{
    int tandem;

    tandem = tp->t_flags & TANDEM;
    if (tp->t_flags & RAW) {
        b_to_q(pb, pe - pb, &tp->t_rawq);
        wakeup((chan_t)&tp->t_rawq);
    } else {
        tp->t_flags &= ~TANDEM;
        while (pb < pe)
            ttyinput(*pb++, tp);
        tp->t_flags |= tandem;
    }
    if (tandem)
        ttyblock(tp);
}

// Place a character on raw TTY input queue, putting in delimiters
// and waking up top half as needed.
// Also echo if required.
// The arguments are the character and the appropriate
// tty structure.
void ttyinput(register int c, register struct tty *tp)
{
    register int t_flags;
    register int col; // t_col before the echo: what advanced it is what may be rubbed out

    tk_nin += 1;
    c &= 0377;
    t_flags = tp->t_flags;
    if (t_flags & TANDEM)
        ttyblock(tp);
    if ((t_flags & RAW) == 0) {
        // Eight bits, not seven: the line carries UTF-8 and bit 8 is data (dev/sc.c).
        // Every character compared for below is ASCII, and no byte of a multi-byte
        // character is.
        //
        // 0377 IS THE EXCEPTION.  It is this queue's own line delimiter (the putc(0377)
        // below, read back by canon()) and also CBRK, which it compares equal to, `char'
        // being unsigned here -- so as data it would be counted twice and t_delct would
        // go negative, wedging the terminal.  No UTF-8 byte is ever 0377, so drop it.
        if (c == 0377)
            return;
        if (tp->t_state & TTSTOP) {
            if (c == tun.t_startc) {
                tp->t_state &= ~TTSTOP;
                ttstart(tp);
                return;
            }
            if (c == tun.t_stopc)
                return;
            tp->t_state &= ~TTSTOP;
            ttstart(tp);
        } else {
            if (c == tun.t_stopc) {
                tp->t_state |= TTSTOP;
                (*cdevsw[major(tp->t_dev)].d_stop)(tp);
                return;
            }
            if (c == tun.t_startc)
                return;
        }
        if (c == tun.t_quitc || c == tun.t_intrc) {
            flushtty(tp);
            c = (c == tun.t_intrc) ? SIGINT : SIGQUIT;
            signal(tp->t_pgrp, c);
            return;
        }
        if (c == '\r' && t_flags & CRMOD)
            c = '\n';
    }
    if (tp->t_rawq.c_cc > TTYHOG) {
        flushtty(tp);
        return;
    }
    if (t_flags & LCASE && c >= 'A' && c <= 'Z')
        c += 'a' - 'A';
    putc(c, &tp->t_rawq);
    if (t_flags & (RAW | CBREAK) || (c == '\n' || c == tun.t_eofc || c == tun.t_brkc)) {
        if ((t_flags & (RAW | CBREAK)) == 0 && putc(0377, &tp->t_rawq) == 0)
            tp->t_delct++;
        wakeup((chan_t)&tp->t_rawq);
    }
    if (t_flags & ECHO) {
        if (c == tp->t_erase && (t_flags & (RAW | CBREAK)) == 0) {
            // RUB THE CHARACTER OUT.  v7 echoed the erase byte like anything else and let the
            // terminal keep the record -- `#' overstruck the text and the paper held it.  A
            // screen prints DEL as nothing at all, so from the first erase the line the eye
            // reads and the line canon() will hand the program drift apart.  "\b \b" is this
            // terminal's version of that record: back up, blank the column, back up again --
            // partab[] classes 2, 0, 2, so net -1 on t_col.
            //
            // THE EDIT ITSELF IS STILL canon()'s.  The erase byte went on the raw queue above
            // like any other and nothing acts on it until the line is read; this is the
            // display half only, and t_echoct is what the two halves agree on.
            //
            // COOKED MODE ONLY: canon() does no erase processing under RAW or CBREAK, so
            // there the byte is data and is echoed as data.  getty reads RAW with ECHO off
            // and rubs out for itself (cmd/getty), and is untouched by any of this.
            if (tp->t_echoct) {
                ttyoutput('\b', tp);
                ttyoutput(' ', tp);
                ttyoutput('\b', tp);
                tp->t_echoct--;
            }
        } else {
            col = tp->t_col;
            ttyoutput(c, tp);
            if (c == tp->t_kill && (t_flags & (RAW | CBREAK)) == 0)
                ttyoutput('\n', tp);

            // WHAT THERE IS TO ERASE, IN COLUMNS THIS LAYER PRINTED.  Nothing here knows
            // partab[]: the column advanced or it did not, which is ttyoutput()'s own answer
            // to the same question.  So a control character counts 0 and its erase rubs out
            // nothing -- the screen was never marked, though canon() still drops the byte --
            // and a UTF-8 continuation byte counts 0 while its lead byte counts 1, canon()
            // backing over the whole sequence for that one rubout.  What the PROGRAM printed
            // is never counted, ttwrite() not coming through here, and that is what keeps an
            // erase at the head of a line off the shell's prompt.
            //
            // A TAB IS THE ONE THIS CANNOT GET RIGHT: under XTABS it echoes as up to eight
            // spaces and counts one, so one erase rubs out one column and leaves the rest
            // standing while canon() removes the tab whole.  Likewise `\' before the erase
            // character, which canon() drops in favour of a literal DEL while the rubout
            // takes the backslash off the screen: its test is on the last byte still in the
            // canonical buffer, not on the last byte typed (type `\', `a', erase and the
            // backslash is back), so tracking it would mean running the editor twice.  Both
            // are display divergences only -- canon() is untouched and a program reads
            // exactly what it read before.
            //
            // Saturated because this is a byte and a cooked line runs to TTYHOG.
            if (c == '\n' || c == tun.t_eofc || c == tun.t_brkc || c == tp->t_kill)
                tp->t_echoct = 0;
            else if (tp->t_col > col && tp->t_echoct < 0377)
                tp->t_echoct++;
        }
        ttstart(tp);
    }
}

// Send stop character on input overflow.
void ttyblock(register struct tty *tp)
{
    register int x;

    x = q1.c_cc + q2.c_cc;
    if (q1.c_cc > TTYHOG) {
        flushtty(tp);
        tp->t_state &= ~TBLOCK;
    }
    if (x >= TTYHOG / 2) {
        if (putc(tun.t_stopc, &tp->t_outq) == 0) {
            tp->t_state |= TBLOCK;
            tp->t_char++;
            ttstart(tp);
        }
    }
}

// put character on TTY output queue, expanding tabs and handling the CR/NL bit.
// (v7 added delays here too; see the tail of the function for where they went.)
// It is called both from the top half for output, and from
// interrupt level for echoing.
// The arguments are the character and the tty structure.
void ttyoutput(register int c, register struct tty *tp)
{
    register char *colp;

    tk_nout += 1;
    // Ignore EOT in normal mode to avoid hanging up
    // certain terminals.
    // In raw mode dump the char unchanged.

    if ((tp->t_flags & RAW) == 0) {
        c &= 0377; // eight bits wide in cooked mode too -- see the tail of this function
        if (c == CEOT)
            return;
    } else {
        putc(c, &tp->t_outq);
        return;
    }

    // Turn tabs to spaces as required
    if (c == '\t' && (tp->t_flags & TBDELAY) == XTABS) {
        c = 8;
        do
            ttyoutput(' ', tp);
        while (--c >= 0 && tp->t_col & 07);
        return;
    }
    // for upper-case-only terminals,
    // generate escapes.
    if (tp->t_flags & LCASE) {
        colp = "({)}!|^~'`";
        while (*colp++)
            if (c == *colp++) {
                ttyoutput('\\', tp);
                c = colp[-2];
                break;
            }
        if ('a' <= c && c <= 'z')
            c += 'A' - 'a';
    }
    // turn <nl> to <cr><lf> if desired.
    if (c == '\n' && tp->t_flags & CRMOD)
        ttyoutput('\r', tp);
    putc(c, &tp->t_outq);

    // COLUMN BOOKKEEPING, AND NOTHING ELSE.  v7 also computed a delay here and queued it
    // as a byte with bit 8 set; that is gone, because on an eight-bit line (dev/sc.c) a
    // queued byte is data and bit 8 cannot also be a mark.  Nothing on this machine has a
    // carriage to wait for -- the finding that took the padding out of lib/libtermcap.
    //
    // A UTF-8 byte has no partab[] entry (128 ASCII classes), and only a lead byte moves
    // the column: `привет' is six columns, so a tab after it stops where the eye says.
    colp = &tp->t_col;
    if (c >= 0200) {
        if ((c & 0300) != 0200)
            (*colp)++;
        return;
    }
    switch (partab[c] & 077) {
    // ordinary
    case 0:
        (*colp)++;

    // non-printing
    case 1:
        break;

    // backspace
    case 2:
        if (*colp)
            (*colp)--;
        break;

    // newline
    case 3:
        *colp = 0;
        break;

    // tab
    case 4:
        *colp |= 07;
        (*colp)++;
        break;

    // vertical motion
    case 5:
        break;

    // carriage return
    case 6:
        *colp = 0;
    }
}

// Start output on the typewriter. It is used from the top half
// after some characters have been put on the output queue,
// from the interrupt routine to transmit the next
// character, and after a timeout has finished.
void ttstart(register struct tty *tp)
{
    register int s;

    s = spl5();
    // v7 tested TIMEOUT too; nothing sets it now that there are no delays (sys/tty.h).
    if ((tp->t_state & (TTSTOP | BUSY)) == 0)
        (*tp->t_oproc)(tp);
    splx(s);
}

// Called from device's read routine after it has
// calculated the tty-structure given as argument.
int ttread(register struct tty *tp)
{
    if ((tp->t_state & CARR_ON) == 0)
        return (0);
    if (tp->t_canq.c_cc || canon(tp))
        while (tp->t_canq.c_cc && passc(getc(&tp->t_canq)) >= 0)
            ;
    return (tp->t_rawq.c_cc + tp->t_canq.c_cc);
}

// Called from the device's write routine after it has
// calculated the tty-structure given as argument.
void ttwrite(register struct tty *tp)
{
    register int c;

    if ((tp->t_state & CARR_ON) == 0)
        return;
    while (u.u_count) {
        spl5();
        while (tp->t_outq.c_cc > TTHIWAT) {
            ttstart(tp);
            if (tp->t_outq.c_cc == 0)
                break;
            tp->t_state |= ASLEEP;
            sleep((chan_t)&tp->t_outq, TTOPRI);
        }
        spl0();
        if ((c = cpass()) < 0)
            break;
        ttyoutput(c, tp);
    }
    ttstart(tp);
    return;
}
