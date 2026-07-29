/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 1999 Robert Nordier. All rights reserved. */

//
// The expression stack -- see stak.h for what it is and how it is used.
//
// Two things changed here, and both are about units.  v7's round() was a bit mask over
// a modulus of BYTESPERWORD, which works only when that is a power of two and it is 6
// on this machine; getstak() and endstak() take sizeup() and wordup() from defs.h
// instead.  And BRKINCR is now one page of the break rather than 512 bytes, because
// that is the unit the break is actually granted in (brkincr.h) -- the comparisons in
// locstak() and stakchk() are char * differences, i.e. char-units, and BRKINCR is in
// char-units too, so they still say what they said.
//
#include "defs.h"

// The start of the item being built.  Points at the empty string until the first
// addblok() gives the stack somewhere real to live.
STKPTR stakbot = nullstr;

// ======== storage allocation ========

//
// Allocate the requested amount of stack.
//
STKPTR getstak(INT asize)
{
    STKPTR oldstak;
    INT size;

    size    = sizeup(asize);
    oldstak = stakbot;
    staktop = stakbot += size;
    return oldstak;
}

//
// Set up the stack for local use; should be followed by `endstak'.
//
STKPTR locstak(void)
{
    if (brkend - stakbot < BRKINCR) {
        //
        // v7 IGNORED THIS FAILURE.  Its setbrk() handed back the old break and nothing
        // looked, because the shell expected to learn about exhaustion the other way:
        // it would keep writing, run off the end of the break, take a memory fault, and
        // fault() would either grow the arena or say `no space'.  That is a real
        // mechanism under the kernel and it is kept -- but it is not the only one
        // available now that setbrk() returns a flag (setbrk.c), and it is not
        // available at all under b6sim, whose signal() implements only SIG_DFL and
        // SIG_IGN.  Without this test a script that exhausts the 28 pages of user space
        // wanders off into unmapped memory instead of being told.
        //
        if (!setbrk((INT)brkincr))
            error(nospace);
        if (brkincr < BRKMAX)
            brkincr += BRKPAGE;
    }
    return stakbot;
}

//
// Take another increment of break, for chkstak() in stak.h.
//
void growstak(void)
{
    if (!setbrk((INT)brkincr))
        error(nospace);
    if (brkincr < BRKMAX)
        brkincr += BRKPAGE;
}

//
// Write one character at `p' in the stored form defs.h describes -- a quoted character,
// and a literal QESC whether quoted or not, become the two bytes QESC c -- and return the
// cursor past it.
//
// EVERY CHARACTER THAT REACHES THE STACK FROM OUTSIDE GOES THROUGH HERE -- a variable's
// value, a command substitution's output, a directory entry's name, a here-document line.
// That is the first of defs.h's three invariants, and it is what stops a raw 0377 in
// someone's data being read back later as a quoting mark.
//
// It takes a CURSOR rather than working on staktop, because half the shell builds its
// items through a local pointer (word(), split(), addg(), copy()) and only the other half
// pushes through staktop; stak.h's pushq() is this for the second kind.
//
// It does its own bounds check, for TWO bytes.  chkstak() promises one, and endstak()
// then writes the terminator without any check at all, so a caller that tested once and
// laid down a pair would run off the break at one byte in six.
//
STRING putq(STRING p, INT c)
{
    if ((c & QUOTE) || (c & LOBYTE) == QESC) {
        chkstak(p + 1);
        *p++ = QESC;
    } else {
        chkstak(p);
    }
    *p++ = c;
    return p;
}

//
// Where the stack is now, to be handed to tdystak() later to unwind back to.  execute()
// takes one of these on the way in and uses it on the way out, so that each command
// hands back the scratch space it used.
//
STKPTR savstak(void)
{
    // v7 asserted staktop==stakbot here, through a macro defined as `;'.
    return stakbot;
}

//
// Tidy up after `locstak': terminate the item, then commit it.
//
STKPTR endstak(STRING argp)
{
    STKPTR oldstak;

    *argp++ = 0;
    oldstak = stakbot;

    //
    // This is the line that makes every (ARGPTR)/(COMPTR)/(STRING *) cast in the shell
    // legal: it is what leaves the next item -- and so the next node the parser builds
    // -- at byte #0 of a word.  A cast from char * to a node pointer FLOORS, it does
    // not round, so a stack top left mid-word would silently address the wrong word.
    //
    stakbot = staktop = wordup(argp);
    return oldstak;
}

//
// Try to bring the stack back to x, returning the blocks the heap has grown over.
//
void tdystak(STKPTR x)
{
    while (ADR(stakbsy) > ADR(x)) {
        shfree(stakbsy);
        stakbsy = stakbsy->word;
    }
    staktop = stakbot = max(ADR(x), ADR(stakbas));
    rmtemp(x);
}

//
// Give a couple of increments back if the stack is sitting on that much slack.
//
void stakchk(void)
{
    if ((brkend - stakbas) > BRKINCR + BRKINCR)
        setbrk(-BRKINCR);
}

//
// Copy a string onto the stack and finish the item in one go: locstak() to get room,
// movstr() to fill it, endstak() to commit it.
//
STKPTR cpystak(STKPTR x)
{
    return endstak(movstr(x, locstak()));
}
