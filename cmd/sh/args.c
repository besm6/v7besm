/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 1999 Robert Nordier. All rights reserved. */

//
// Option handling and the positional parameters.
//
#include "defs.h"

static STRING *copyargs(STRING from[], INT n);
static DOLPTR dolh;

//
// $- : the letters of the flags currently set.
//
// v7 sized this [10], one per flag in flagchar[] below -- and then wrote the flags AND
// a terminating NUL into it, so `sh -xnvtsierku' put eleven bytes in ten and smashed
// the following word.  Sized for what it holds.
//
CHAR flagadr[12];

CHAR flagchar[] = { 'x', 'n', 'v', 't', 's', 'i', 'e', 'r', 'k', 'u', 0 };
INT flagval[]   = {
    execpr, noexec, readpr, oneflg, stdflg, intflg, errflg, rshflg, keyflg, setflg, 0
};

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

void clearup(void)
{
    // force `for' $* lists to go away
    while ((argfor = freeargs(argfor)) != 0)
        ;

    // clean up io files
    while (pop())
        ;
}

DOLPTR useargs(void)
{
    if (dolh) {
        dolh->doluse++;
        dolh->dolnxt  = argfor;
        return argfor = dolh;
    }
    return 0;
}
