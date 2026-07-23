// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// getpass(prompt) -- read a line from the terminal with the echo turned off.
//
// v7's, and the eight-character answer is v7's too: crypt() reads at most eight
// characters of a password, so there was never a reason to keep more.
//
// The interrupt is caught -- ignored, rather -- for the duration, so that a ^C between
// turning the echo off and turning it back on cannot leave the terminal deaf.  Only
// SIG_IGN is asked for here, so this never needed the signal frame; what it needs is a
// terminal, and that is what keeps it untested (below).
//
// NOT COVERED BY lib/test/: it opens /dev/tty and would sit there waiting for someone
// to type, which a diff-against-.expected harness cannot arrange.
//
// Two changes from v7: the dispositions are `void (*)(int)' per <signal.h>, and the
// prompt goes out through fputs() rather than fprintf(), which passed a caller's string
// as a format -- harmless in 1979 and a hole worth closing now that stdio has a real
// printf engine behind it.
//
// No header declares it; a caller declares it itself.
//
#include <sgtty.h>
#include <signal.h>
#include <stdio.h>

int gtty(int fd, struct sgttyb *buf);
int stty(int fd, struct sgttyb *buf);

char *getpass(const char *prompt)
{
    struct sgttyb ttyb;
    int flags, c;
    char *p;
    FILE *fi;
    static char pbuf[9];
    void (*sig)(int);

    if ((fi = fopen("/dev/tty", "r")) == NULL)
        fi = stdin;
    else
        setbuf(fi, (char *)NULL);

    sig = signal(SIGINT, SIG_IGN);
    gtty(fileno(fi), &ttyb);
    flags = ttyb.sg_flags;
    ttyb.sg_flags &= ~ECHO;
    stty(fileno(fi), &ttyb);

    fputs(prompt, stderr);
    for (p = pbuf; (c = getc(fi)) != '\n' && c != EOF;) {
        if (p < &pbuf[8])
            *p++ = c;
    }
    *p = '\0';
    fputs("\n", stderr);

    ttyb.sg_flags = flags;
    stty(fileno(fi), &ttyb);
    signal(SIGINT, sig);
    if (fi != stdin)
        fclose(fi);
    return pbuf;
}
