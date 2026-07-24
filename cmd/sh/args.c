/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 1999 Robert Nordier. All rights reserved. */

//
// Option handling and the positional parameters.
//
#include "defs.h"

// Defined below.
static STRING *copyargs(STRING from[], INT n);

// The current list of positional parameters, reference-counted.  setargs() replaces it;
// useargs() takes a reference so that a `for' loop can keep walking the old one.
static DOLPTR dolh;

//
// $- : the letters of the flags currently set.
//
// v7 sized this [10], one per flag in flagchar[] below -- and then wrote the flags AND
// a terminating NUL into it, so `sh -xnvtsierku' put eleven bytes in ten and smashed
// the following word.  Sized for what it holds.
//
CHAR flagadr[12];

// The option letters, and the flag bit each one sets.  The two arrays are parallel: the
// bit for a letter is flagval[] at the same subscript.  Both end with 0.
CHAR flagchar[] = { 'x', 'n', 'v', 't', 's', 'i', 'e', 'r', 'k', 'u', 0 };
// The bit each of those letters sets; same subscript as flagchar[] above.
INT flagval[] = {
    execpr, noexec, readpr, oneflg, stdflg, intflg, errflg, rshflg, keyflg, setflg, 0
};

//
// Read the shell's own options off the front of the command line.
//
// Handles `sh -x', `sh -xv' and `sh -c "command"'; anything else is left alone for the
// script.  Returns how many arguments remain, and fills in $- along the way.
//
INT options(INT argc, STRING *argv)
{
    STRING cp;
    STRING *argp = argv;
    STRING flagc;
    STRING flagp;

    if (argc > 1 && *argp[1] == '-') {
        cp = argp[1];
        flags &= ~(execpr | readpr);
        while (*++cp) {
            flagc = flagchar;

            while (*flagc && *flagc != *cp)
                flagc++;
            if (*cp == *flagc) {
                flags |= flagval[flagc - flagchar];
            } else if (*cp == 'c' && argc > 2 && !comdivset) {
                comdiv    = argp[2];
                comdivset = 1;
                argp[1]   = argp[0];
                argp++;
                argc--;
            } else {
                failed(argv[1], badopt);
            }
        }
        argp[1] = argp[0];
        argc--;
    }

    // set up $-
    flagc = flagchar;
    flagp = flagadr;
    while (*flagc) {
        if (flags & flagval[flagc - flagchar])
            *flagp++ = *flagc;
        flagc++;
    }
    *flagp++ = 0;

    return argc;
}

//
// Replace the positional parameters $1, $2, ... with a new list.
//
// The strings are copied into the arena, because the vector handed in usually lives on
// the expression stack and will not survive the next command.
//
void setargs(STRING argi[])
{
    // count args
    STRING *argp = argi;
    INT argn     = 0;

    // v7 wrote Rcheat(*argp++) != ENDARGS, casting each argument -- a fat char * with
    // its bit-48 marker set -- to an int just to compare it with zero.  It is a null
    // pointer test and says so.
    while (*argp++ != 0)
        argn++;

    // free old ones unless on for loop chain
    freeargs(dolh);
    dolh = (DOLPTR)copyargs(argi, argn); // sets dolv
    assnum(&dolladr, dolc = argn - 1);
}

//
// Drop one reference to a saved parameter list, freeing it and its strings when the last
// user lets go.  Returns the next list on the chain.
//
DOLPTR freeargs(DOLPTR blk)
{
    STRING *argp;
    DOLPTR argr = 0;
    DOLPTR argblk;

    if ((argblk = blk) != 0) {
        argr = argblk->dolnxt;
        if ((--argblk->doluse) == 0) {
            for (argp = (STRING *)argblk->dolarg; *argp != 0; argp++)
                shfree((BLKPTR)*argp);
            shfree((BLKPTR)argblk);
        }
    }
    return argr;
}

//
// Copy n argument strings into one arena block: the reference count, then the vector,
// then a null to end it.  Sets dolv to the vector and returns the block.
//
static STRING *copyargs(STRING from[], INT n)
{
    STRING *np = (STRING *)shalloc(sizeof(STRING *) * n + 3 * BYTESPERWORD);
    STRING *fp = from;
    STRING *pp = np;

    ((DOLPTR)np)->doluse = 1; // use count
    np                   = (STRING *)((DOLPTR)np)->dolarg;
    dolv                 = np;

    while (n--)
        *np++ = make(*fp++);
    *np++ = ENDARGS;
    return pp;
}

//
// Abandon everything the shell was in the middle of -- the `for' loops' saved parameter
// lists, and any input files it had stacked up.  Called on the way out of an error, so
// that the prompt starts clean.
//
void clearup(void)
{
    // force `for' $* lists to go away
    while ((argfor = freeargs(argfor)) != 0)
        ;

    // clean up io files
    while (pop())
        ;
}

//
// Take one more reference to the current parameter list and put it on the `for' chain,
// so that a loop walking $* survives the body running `set'.
//
DOLPTR useargs(void)
{
    if (dolh) {
        dolh->doluse++;
        dolh->dolnxt  = argfor;
        return argfor = dolh;
    }
    return 0;
}
