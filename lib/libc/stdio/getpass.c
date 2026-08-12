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
// Four changes from v7: the dispositions are `void (*)(int)' per <signal.h>, the prompt
// goes out through fputs() rather than fprintf(), which passed a caller's string as a
// format -- harmless in 1979 and a hole worth closing now that stdio has a real printf
// engine behind it -- the stdin fallback is unbuffered too (see below), and THE BOUND ON
// THE COPY IS AN INDEX.
//
// That last one was not tidying up.  v7 wrote `for (p = pbuf; ...) if (p < &pbuf[8])
// *p++ = c;', and until the compiler's fix of 2026-06-17 `<' BETWEEN TWO char * VALUES DID
// NOT ORDER THEM ON THIS MACHINE: a fat pointer carries its byte offset in bits 47-45 above
// its word address in bits 15-1, the offset DECREMENTS as the pointer advances
// (../../../doc/Besm6_Data_Representation.md), and the comparison was of the whole word, so
// the offset field dominated.  pbuf is nine characters, so p started at byte #0 of its first
// word (offset field 5) while &pbuf[8] is byte #2 of the second (offset field 3) -- the test
// was FALSE on the very first iteration, nothing was ever stored, and getpass() returned the
// empty string every time.  The note below on why lib/test/ does not cover this is also why
// nothing caught it.  The relational would work today; the index stays because it is
// cheaper and because this routine has been wrong once already.
//
// <unistd.h> declares it, and is included below so that this definition is checked
// against it.  v7 declared it nowhere and every caller carried its own prototype; task C6
// ended that, and the header says why.  The two lines that redeclared gtty()/stty() went
// with it: <sgtty.h> right above has said so all along.
//
#include <sgtty.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

char *getpass(const char *prompt)
{
    struct sgttyb ttyb;
    int flags, c, n;
    FILE *fi;
    static char pbuf[9];
    void (*sig)(int);

    // UNBUFFERED EITHER WAY.  v7 unbuffered only /dev/tty, and the fallback to stdin then
    // read a whole buffer to get one line -- so a caller reading its own input with
    // read(2) found it gone.  ed -x is the case: it takes the key here and its commands
    // with read(0), and in a shell script, where there is no /dev/tty, getpass() swallowed
    // the script.  This routine's contract is one line, on both paths.
    if ((fi = fopen("/dev/tty", "r")) == NULL)
        fi = stdin;
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
