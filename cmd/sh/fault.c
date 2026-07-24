/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// Fault handling routines.
//
// THE SIGNAL INTERFACE IS C11's, NOT v7's.  <signal.h> here declares
// `void (*signal(int, void (*)(int)))(int)', so a handler takes the signal number and
// returns void, and SIG_DFL/SIG_IGN/SIG_ERR are function pointers rather than the small
// integers 0 and 1 v7 passed and bit-tested.  Three idioms changed with it: fault() has
// the handler shape, `signal(i, 1)' is `signal(i, SIG_IGN)', and ignsig()'s
// `signal(...) & 01' -- which read bit 0 of a returned function pointer to ask "was
// this already ignored?" -- is a comparison against SIG_IGN.
//
#include <signal.h>

#include "defs.h"

STRING trapcom[MAXTRAP];
BOOL trapflg[MAXTRAP];

void fault(INT sig)
{
    INT flag;

    signal(sig, fault);
    if (sig == MEMF) {
        // The arena wants another increment.  v7 tested setbrk()'s result against -1;
        // this tree's sbrk() reports failure as NULL, so setbrk() returns a flag
        // instead of a break address (setbrk.c).
        if (!setbrk((INT)brkincr))
            error(nospace);
    } else if (sig == ALARM) {
        if (flags & waiting)
            done();
    } else {
        flag = (trapcom[sig] ? TRAPSET : SIGSET);
        trapnote |= flag;
        trapflg[sig] |= flag;
    }
}

void stdsigs(void)
{
    ignsig(QUIT);
    getsig(INTR);
    getsig(MEMF);
    getsig(ALARM);
}

//
// Ignore signal n, and report whether it was ALREADY being ignored -- which is how the
// shell learns that it was started with the signal ignored and must leave it that way.
//
INT ignsig(INT n)
{
    INT i = n;
    INT s;

    s = (signal(i, SIG_IGN) == SIG_IGN);
    if (s == 0)
        trapflg[i] |= SIGMOD;
    return s;
}

void getsig(INT n)
{
    INT i = n;

    if (trapflg[i] & SIGMOD || ignsig(i) == 0)
        signal(i, fault);
}

void oldsigs(void)
{
    INT i;
    STRING t;

    i = MAXTRAP;
    while (i--) {
        t = trapcom[i];
        if (t == 0 || *t)
            clrsig(i);
        trapflg[i] = 0;
    }
    trapnote = 0;
}

void clrsig(INT i)
{
    shfree((BLKPTR)trapcom[i]);
    trapcom[i] = 0;
    if (trapflg[i] & SIGMOD) {
        signal(i, fault);
        trapflg[i] &= ~SIGMOD;
    }
}

//
// Run the trap commands for whatever has fired since the last check.
//
void chktrap(void)
{
    INT i = MAXTRAP;
    STRING t;

    trapnote &= ~TRAPSET;
    while (--i) {
        if (trapflg[i] & TRAPSET) {
            trapflg[i] &= ~TRAPSET;
            if ((t = trapcom[i]) != 0) {
                INT savxit = exitval;
                execexp(t, 0, 0);
                exitval = savxit;
                exitset();
            }
        }
    }
}
