/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// Error handling.
//
// The four exits here are declared _Noreturn in defs.h, and that is not decoration.
// Several callers -- stoi() in print.c, the errno switch in service.c, chkopen() and
// create() in io.c -- end a branch with failed() and then fall off the end of a
// function that has a return value.  That is correct only because failed() does not
// come back; without _Noreturn it is undefined behaviour, and a C11 compiler's licence
// to assume the branch is unreachable and delete the check that guards it.
//
#include <stdlib.h>
#include <unistd.h>

#include "defs.h"

//
// Rewrite $? from the exit status of the command that just finished.  Called after every
// one of them.
//
void exitset(void)
{
    assnum(&exitadr, exitval);
}

//
// Find out if it is time to go away.  `trapnote' is set to SIGSET when a fault is seen
// and no trap has been set.
//
void sigchk(void)
{
    if (trapnote & SIGSET)
        exitsh(SIGFAIL);
}

//
// Find out if the recursion has run the MACHINE stack out.  NOTHING ELSE CAN TELL THE
// SHELL THIS: an overflow wraps onto word 0 and rewrites the shell's own image rather
// than faulting (README.md, "The stack").  The measurement is a char * difference from
// the floor main() recorded -- the arithmetic stak.c already does with `brkend -
// stakbot', and no pointer is made up out of an integer.  DOES NOT RETURN when it fires.
//
void deepchk(void)
{
    CHAR here;

    if ((STKPTR)&here - stackfloor > STAKROOM)
        error(toodeep);
}

//
// Report "sh: s1: s2" and abandon the command.  s2 may be null, in which case only s1 is
// printed -- that is what error() below is.  DOES NOT RETURN.
//
_Noreturn void failed(STRING s1, STRING s2)
{
    prp();
    prs(s1);
    if (s2) {
        prs(colon);
        prs(s2);
    }
    newline();
    exitsh(ERROR);
}

//
// Report "sh: s" and abandon the command.  DOES NOT RETURN.
//
_Noreturn void error(STRING s)
{
    failed(s, NIL);
}

//
// Arrive here from `FATAL' errors:
//   a) the exit command,
//   b) the default trap,
//   c) a fault with no trap set.
// The action is to return to command level, or to exit.
//
_Noreturn void exitsh(INT xno)
{
    exitval = xno;
    if ((flags & (forked | errflg | ttyflg)) != ttyflg) {
        done();
    } else {
        clearup();
        longjmp(errshell, 1);
    }

    //
    // Unreachable, and said out loud because the compiler cannot know it: this tree's
    // <setjmp.h> deliberately does NOT declare longjmp _Noreturn, so without this the
    // else branch would fall off the end of a _Noreturn function -- undefined
    // behaviour, and exactly the licence the callers who rely on it must not give.
    //
    _exit(exitval);
}

//
// Leave the shell for good: run the trap the user set for exit (`trap cmd 0'), remove the
// here-document temp files, and exit with the status in `exitval'.  DOES NOT RETURN.
//
_Noreturn void done(void)
{
    STRING t;

    if ((t = trapcom[0]) != 0) {
        trapcom[0] = 0; // should free but not long
        execexp(t, 0, 0);
    }
    rmtemp(0);
    exit(exitval);
}

//
// Unlink the here-document temp files above `base'.
//
// v7 declared base an IOPTR and compared it against iotemp directly, but tdystak()
// calls this with a stack address -- a char *.  On this machine those are not the same
// kind of pointer at all (a fat one and a bare word address), so the parameter says
// which it is and the comparison converts the other side.
//
//
// Unlink the here-document temp files above `base'.
//
// Each here-document in a command gets a file in /tmp holding its text, and this is what
// removes them once the command is finished with; `base' says how far back to go, so
// that a nested command does not delete the files of the one that contains it.
//
// v7 declared base an IOPTR and compared it against iotemp directly, but tdystak()
// calls this with a stack address -- a char *.  On this machine those are not the same
// kind of pointer at all (a fat one and a bare word address), so the parameter says
// which it is and the comparison converts the other side.
//
void rmtemp(STKPTR base)
{
    while (ADR(iotemp) > base) {
        unlink(iotemp->ioname);
        iotemp = iotemp->iolst;
    }
}
