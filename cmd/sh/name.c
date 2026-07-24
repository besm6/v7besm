/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 1999 Robert Nordier. All rights reserved. */

//
// Variable and string handling: the name tree.
//
// TWO FUNCTIONS ARE RENAMED.  v7 called them getenv() and setenv(); libc owns the first
// of those names (lib/libc/gen/getenv.c) with an entirely different signature, and
// <stdlib.h> declares it.  They are readenv() and shenv() here.  Nothing else about
// them changed: readenv() walks `environ' into the name tree, shenv() builds the
// environment vector for an exec on the expression stack.
//
#include <unistd.h>

#include "defs.h"

static BOOL chkid(STRING nam);
static void namwalk(NAMPTR np);
static STRING staknam(NAMPTR n);

NAMNOD ps2nod  = { (NAMPTR)NIL, (NAMPTR)NIL, ps2name },
       fngnod  = { (NAMPTR)NIL, (NAMPTR)NIL, fngname },
       pathnod = { (NAMPTR)NIL, (NAMPTR)NIL, pathname },
       ifsnod = { (NAMPTR)NIL, (NAMPTR)NIL, ifsname }, ps1nod = { &pathnod, &ps2nod, ps1name },
       homenod = { &fngnod, &ifsnod, homename }, mailnod = { &homenod, &ps1nod, mailname };

NAMPTR namep = &mailnod;

INT syslook(STRING w, SYSPTR syswds)
{
    CHAR first;
    STRING s;
    SYSPTR syscan;

    syscan = syswds;
    first  = *w;

    while ((s = syscan->sysnam) != 0) {
        if (first == *s && eq(w, s))
            return syscan->sysval;
        syscan++;
    }
    return 0;
}

void setlist(ARGPTR arg, INT xp)
{
    while (arg) {
        STRING s = mactrim(arg->argval);
        setname(s, xp);
        arg = arg->argnxt;
        if (flags & execpr) {
            prs(s);
            if (arg)
                blank();
            else
                newline();
        }
    }
}

void setname(STRING argi, INT xp)
{
    STRING argscan = argi;
    NAMPTR n;

    if (letter(*argscan)) {
        while (alphanum(*argscan))
            argscan++;
        if (*argscan == '=') {
            *argscan   = 0;
            n          = lookup(argi);
            *argscan++ = '=';
            attrib(n, xp);
            if (xp & N_ENVNAM)
                n->namenv = n->namval = argscan;
            else
                assign(n, argscan);
            return;
        }
    }
    failed(argi, notid);
}

void replace(STRING *a, STRING v)
{
    shfree((BLKPTR)*a);
    *a = make(v);
}

void dfault(NAMPTR n, STRING v)
{
    if (n->namval == 0)
        assign(n, v);
}

void assign(NAMPTR n, STRING v)
{
    if (n->namflg & N_RDONLY)
        failed(n->namid, wtfailed);
    else
        replace(&n->namval, v);
}

INT readvar(STRING *names)
{
    SHFILEBLK fb;
    SHFILE f = &fb;
    CHAR c;
    INT rc   = 0;
    NAMPTR n = lookup(*names++); // done now to avoid storage mess

    //
    // AN OFFSET, IN AN INT.  v7 held relstak()'s result in a STKPTR, because a byte
    // offset and a char * are the same sixteen bits on a PDP-11.  Here a char * is a
    // fat pointer -- a bit-48 marker, a byte offset within the word, a word address --
    // and an integer cast into one is not a pointer at all.  See stak.h.
    //
    INT rel = relstak();

    push(f);
    initf(dup(0));
    if (lseek(0, (off_t)0, SEEK_CUR) == -1)
        f->fsiz = 1;

    for (;;) {
        c = nextc(0);
        if ((*names && any(c, ifsnod.namval)) || eolchar(c)) {
            zerostak();
            assign(n, absstak(rel));
            setstak(rel);
            if (*names)
                n = lookup(*names++);
            else
                n = 0;
            if (eolchar(c))
                break;
        } else {
            chkstak(staktop);
            pushstak(c);
        }
    }
    while (n) {
        assign(n, nullstr);
        if (*names)
            n = lookup(*names++);
        else
            n = 0;
    }

    if (eof)
        rc = 1;
    lseek(0, (off_t)(f->fnxt - f->fend), SEEK_CUR);
    pop();
    return rc;
}

void assnum(STRING *p, INT i)
{
    itos(i);
    replace(p, numbuf);
}

STRING make(STRING v)
{
    STRING p;

    if (v) {
        movstr(v, p = shalloc(length(v)));
        return p;
    }
    return 0;
}

NAMPTR lookup(STRING nam)
{
    NAMPTR nscan = namep;
    NAMPTR *prev = &namep;
    INT LR;

    if (!chkid(nam))
        failed(nam, notid);

    while (nscan) {
        if ((LR = cf(nam, nscan->namid)) == 0)
            return nscan;
        else if (LR < 0)
            prev = &(nscan->namlft);
        else
            prev = &(nscan->namrgt);
        nscan = *prev;
    }

    // add name node
    nscan         = (NAMPTR)shalloc(sizeof *nscan);
    nscan->namlft = nscan->namrgt = (NAMPTR)NIL;
    nscan->namid                  = make(nam);
    nscan->namval                 = 0;
    nscan->namflg                 = N_DEFAULT;
    nscan->namenv                 = 0;
    return *prev                  = nscan;
}

static BOOL chkid(STRING nam)
{
    CHAR *cp = nam;

    if (!letter(*cp)) {
        return FALSE;
    } else {
        while (*++cp) {
            if (!alphanum(*cp))
                return FALSE;
        }
    }
    return TRUE;
}

static void (*namfn)(NAMPTR);

void namscan(void (*fn)(NAMPTR))
{
    namfn = fn;
    namwalk(namep);
}

static void namwalk(NAMPTR np)
{
    if (np) {
        namwalk(np->namlft);
        (*namfn)(np);
        namwalk(np->namrgt);
    }
}

void printnam(NAMPTR n)
{
    STRING s;

    sigchk();
    if ((s = n->namval) != 0) {
        prs(n->namid);
        prc('=');
        prs(s);
        newline();
    }
}

static STRING staknam(NAMPTR n)
{
    STRING p;

    //
    // Unlike every other stack writer, this one does not go through locstak() first --
    // v7 wrote straight at staktop.  The length is the name plus the value, both
    // unbounded, so ask for the room before taking it.
    //
    p = staktop;
    while (brkend - p < length(n->namid) + length(n->namval) + 1)
        growstak();
    p = movstr(n->namid, p);
    p = movstr("=", p);
    p = movstr(n->namval, p);
    return getstak(p + 1 - ADR(stakbot));
}

void exname(NAMPTR n)
{
    if (n->namflg & N_EXPORT) {
        shfree((BLKPTR)n->namenv);
        n->namenv = make(n->namval);
    } else {
        shfree((BLKPTR)n->namval);
        n->namval = make(n->namenv);
    }
}

void printflg(NAMPTR n)
{
    if (n->namflg & N_EXPORT) {
        prs(export);
        blank();
    }
    if (n->namflg & N_RDONLY) {
        prs(readonly);
        blank();
    }
    if (n->namflg & (N_EXPORT | N_RDONLY)) {
        prs(n->namid);
        newline();
    }
}

//
// v7's getenv(): read the environment vector into the name tree.
//
void readenv(void)
{
    STRING *e = environ;

    while (*e)
        setname(*e++, N_ENVNAM);
}

static INT namec;

void countnam(NAMPTR n)
{
    (void)n;
    namec++;
}

static STRING *argnam;

void pushnam(NAMPTR n)
{
    if (n->namval)
        *argnam++ = staknam(n);
}

//
// v7's setenv(): build the environment vector for an exec, on the stack.
//
STRING *shenv(void)
{
    STRING *er;

    namec = 0;
    namscan(countnam);
    argnam = er = (STRING *)getstak(namec * BYTESPERWORD + BYTESPERWORD);
    namscan(pushnam);
    *argnam++ = 0;
    return er;
}
