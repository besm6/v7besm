/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 1999 Robert Nordier. All rights reserved. */

//
// The shell: startup, the profile, and the command loop.
//
#include <setjmp.h>
#include <sgtty.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "defs.h"
#include "sym.h"

//
// How long to wait at an interactive prompt before giving up and logging out.  v7 kept
// this in a timeout.h of its own, set to 0 -- no timeout -- and so does this; alarm(0)
// cancels rather than schedules.
//
#define TIMEOUT 0

UFD output           = 2;
static BOOL beenhere = FALSE;

//
// The name of the here-document temp files: "/tmp/sh" then the pid then a serial
// number, both written in by settmp() and tmpfil().  v7 sized it [20] against a pid of
// at most five digits; an INT here is a 41-bit signed value, so both numbers are sized
// by what itos() can produce (print.c).
//
CHAR tmpout[TMPNAM + NUMBUF + NUMBUF] = "/tmp/sh-";

static SHFILEBLK stdfile;
SHFILE standin = &stdfile;

static void exfile(BOOL prof);
static void Ldup(INT fa, INT fb);

int main(int c, char **v)
{
    INT rflag = ttyflg;

    // initialise storage allocation
    stdsigs();
    setbrk(BRKINCR);
    addblok((POS)0);

    // set names from userenv
    readenv();

    // look for restricted
    // IF c>0 ANDF any('r', *v) THEN rflag=0 FI

    // look for options
    dolc = options(c, v);
    if (dolc < 2)
        flags |= stdflg;
    if ((flags & stdflg) == 0)
        dolc--;
    dolv = v + c - dolc;
    dolc--;

    // return here for shell file execution
    setjmp(subshell);

    // number of positional parameters
    assnum(&dolladr, dolc);
    cmdadr = dolv[0];

    // set pidname
    assnum(&pidadr, getpid());

    // set up temp file names
    settmp();

    // default ifs
    dfault(&ifsnod, sptbnl);

    if ((beenhere++) == FALSE) {
        // ? profile
        if (*cmdadr == '-' && (input = pathopen(nullstr, profile)) >= 0) {
            exfile(rflag);
            flags &= ~ttyflg;
        }
        if (rflag == 0)
            flags |= rshflg;

        // open input file if specified
        if (comdivset) {
            estabf(comdiv);
            input = -1;
        } else {
            input = ((flags & stdflg) ? 0 : chkopen(cmdadr));
            //
            // v7 wrote `comdiv--' here, so that a shell started without -c held
            // (char *)-1 and every later `IF comdiv' read true.  Decrementing a null
            // FAT pointer underflows its byte-offset field into its word address, and
            // whether the result is non-zero is compiler trivia -- so the fact that -c
            // was given lives in a flag of its own (glob.c) and comdiv stays a pointer.
            //
            comdivset = 1;
        }
    }
    //
    // v7's else branch was `*execargs = (char *)dolv', which planted the argument
    // vector at a magic address for ps(1) to find.  <execargs.h> is a PDP-11 kernel
    // interface and there is no equivalent here, so the cosmetics are gone.
    //

    exfile(0);
    done();
}

static void exfile(BOOL prof)
{
    L_INT mailtime = 0;
    INT userid;
    struct stat statb;

    // move input
    if (input > 0) {
        Ldup(input, INIO);
        input = INIO;
    }

    // move output to safe place
    if (output == 2) {
        Ldup(dup(2), OTIO);
        output = OTIO;
    }

    userid = getuid();

    //
    // Decide whether interactive.  v7 asked gtty() itself -- and handed it a `struct
    // stat' rather than a `struct sgttyb', which worked only because K&R checked
    // nothing and gtty writes fewer words than a stat buffer has.  isatty() is the
    // question that was being asked (lib/libc/gen/isatty.c), and it answers 1 for a
    // terminal where gtty answers 0 for success.
    //
    if ((flags & intflg) || ((flags & oneflg) == 0 && isatty(output) && isatty(input))) {
        dfault(&ps1nod, (userid ? stdprompt : supprompt));
        dfault(&ps2nod, readmsg);
        flags |= ttyflg | prompt;
        ignsig(KILL);
    } else {
        flags |= prof;
        flags &= ~prompt;
    }

    if (setjmp(errshell) && prof) {
        close(input);
        return;
    }

    // error return here
    loopcnt = breakcnt = peekc = 0;
    iopend                     = 0;
    if (input >= 0)
        initf(input);

    // command loop
    for (;;) {
        tdystak(0);
        stakchk(); // may reduce sbrk
        exitset();
        if ((flags & prompt) && standin->fstak == 0 && !eof) {
            if (mailnod.namval && stat(mailnod.namval, &statb) >= 0 && statb.st_size &&
                (statb.st_mtime != mailtime) && mailtime)
                prs(mailmsg);
            mailtime = statb.st_mtime;
            prs(ps1nod.namval);
            alarm(TIMEOUT);
            flags |= waiting;
        }

        trapnote = 0;
        peekc    = readc();
        if (eof)
            return;
        alarm(0);
        flags &= ~waiting;
        execute(cmd(NL, MTFLG), 0, 0, 0);
        eof |= (flags & oneflg);
    }
}

void chkpr(CHAR eor)
{
    if ((flags & prompt) && standin->fstak == 0 && eor == NL)
        prs(ps2nod.namval);
}

void settmp(void)
{
    itos(getpid());
    serial = 0;
    tmpnam = movstr(numbuf, &tmpout[TMPNAM]);
}

//
// Move fd `fa' to `fb' and mark it close-on-exec, so that the shell's own input and
// output survive where a command cannot see them.
//
// v7 spelled the move dup(fa|DUPFLG, fb) -- the PDP-11 kernel's dup2 before it had a
// name, with the flag bit in the high byte of the first argument.  libc has dup2 here
// and the kernel's dup always takes two arguments, so say it directly.
//
static void Ldup(INT fa, INT fb)
{
    dup2(fa, fb);
    close(fa);
    ioctl(fb, FIOCLEX, 0);
}
