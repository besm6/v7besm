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
// Three changes from v7: the dispositions are `void (*)(int)' per <signal.h>, the prompt
// goes out through fputs() rather than fprintf(), which passed a caller's string as a
// format -- harmless in 1979 and a hole worth closing now that stdio has a real printf
// engine behind it -- and THE BOUND ON THE COPY IS AN INDEX.
//
// That last one was not tidying up.  v7 wrote `for (p = pbuf; ...) if (p < &pbuf[8])
// *p++ = c;', and `<' BETWEEN TWO char * VALUES DOES NOT ORDER THEM ON THIS MACHINE.  A
// fat pointer carries its byte offset in bits 47-45 and its word address in bits 15-1, and
// the offset DECREMENTS as the pointer advances (../../../doc/Besm6_Data_Representation.md);
// there is no relational helper, so `<' compiles to an integer comparison of the whole
// word and the offset field dominates the address field.  pbuf is nine characters, so p
// starts at byte #0 of its first word (offset field 5) while &pbuf[8] is byte #2 of the
// second (offset field 3) -- the test was FALSE on the very first iteration, nothing was
// ever stored, and getpass() returned the empty string every time.  The note below on why
// lib/test/ does not cover this is also why nothing caught it.
// ../../../cmd/ls/README.md names the hazard; lib/libtermcap/termcap.c had four more.
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
    int flags, c, n;
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
    for (n = 0; (c = getc(fi)) != '\n' && c != EOF;) {
        if (n < 8)
            pbuf[n++] = c;
    }
    pbuf[n] = '\0';
    fputs("\n", stderr);

    ttyb.sg_flags = flags;
    stty(fileno(fi), &ttyb);
    signal(SIGINT, sig);
    if (fi != stdin)
        fclose(fi);
    return pbuf;
}
