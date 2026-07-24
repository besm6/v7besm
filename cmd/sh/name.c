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

// Defined below.
static BOOL chkid(STRING nam);
static void namwalk(NAMPTR np);
static STRING staknam(NAMPTR n);

//
// The seven variables that always exist, pre-built rather than allocated: the shell reads
// them itself, so they have to be there before the first script line runs.  Each holds a
// left child, a right child and a name; the values start empty.  They are hand-arranged
// into a balanced tree, whose root is the `namep' below.
//
NAMNOD ps2nod  = { (NAMPTR)NIL, (NAMPTR)NIL, ps2name },
       fngnod  = { (NAMPTR)NIL, (NAMPTR)NIL, fngname },
       pathnod = { (NAMPTR)NIL, (NAMPTR)NIL, pathname },
       ifsnod = { (NAMPTR)NIL, (NAMPTR)NIL, ifsname }, ps1nod = { &pathnod, &ps2nod, ps1name },
       homenod = { &fngnod, &ifsnod, homename }, mailnod = { &homenod, &ps1nod, mailname };

//
// THE NAME TREE: every shell variable, kept as a binary search tree ordered by name, so
// that a lookup is a few comparisons rather than a walk of the whole list and so that
// `set' can print them in order for free (namwalk() visits left, self, right).
//
// The seven nodes above are built in rather than allocated, because the shell reads them
// itself and they must exist before any script runs.  They are hand-arranged into a
// balanced tree, with mailnod at the root.
//
NAMPTR namep = &mailnod;

//
// Look a word up in one of the keyword tables and return its number, or 0 if it is not
// there.  Comparing the first character before calling cf() is the whole optimisation:
// most words fail on it.
//
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

//
// Perform a whole list of `name=value' assignments -- the ones written in front of a
// command, as in `TZ=UTC LANG=C date'.  xp is the attribute to give each variable, which
// is N_EXPORT when the command is about to be exec'd so that it inherits them.
//
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

//
// Perform one `name=value' assignment.
//
// The `=' is temporarily overwritten with a NUL so that the name can be looked up
// without copying it, then put back.  Anything that is not a valid variable name is an
// error.
//
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

//
// Replace the string at *a with a fresh copy of v, freeing whatever was there.
//
void replace(STRING *a, STRING v)
{
    shfree((BLKPTR)*a);
    *a = make(v);
}

//
// Give a variable a value only if it does not have one yet.  How the shell supplies the
// defaults for IFS, PS1 and PS2 without overriding what the user set.
//
void dfault(NAMPTR n, STRING v)
{
    if (n->namval == 0)
        assign(n, v);
}

//
// Assign to a variable, refusing if it was made readonly.
//
void assign(NAMPTR n, STRING v)
{
    if (n->namflg & N_RDONLY)
        failed(n->namid, wtfailed);
    else
        replace(&n->namval, v);
}

//
// The `read' built-in: read one line of input and split it across the named variables.
//
// The split is at the characters in IFS.  The LAST name gets everything that is left,
// separators and all, and any names beyond the words available are set to the empty
// string.  Returns 1 at end of file, which is what makes `while read x' terminate.
//
// The lseek at the end matters: the shell buffers its input, so it has usually read
// further than the line it just consumed, and this pushes the file back to where the
// line ended -- otherwise a command run afterwards would find its input already eaten.
//
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

//
// Set a string to the decimal form of a number.  How $?, $# and $$ are kept up to date.
//
void assnum(STRING *p, INT i)
{
    itos(i);
    replace(p, numbuf);
}

//
// Copy a string into the arena so that it outlives the expression stack, which is reused
// by the very next command.  A null in gives a null back.
//
STRING make(STRING v)
{
    STRING p;

    if (v) {
        movstr(v, p = shalloc(length(v)));
        return p;
    }
    return 0;
}

//
// Find a variable by name in the name tree, adding it if it is not there.
//
// Always returns a node, so callers never test for failure: an unset variable is a node
// with a null value, not a missing node.  A name that is not a valid identifier is an
// error rather than a new variable.
//
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

//
// Is this a legal variable name -- a letter or underscore, then letters, underscores and
// digits?
//
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

// The function namscan() is walking the tree with.  A global because namwalk() recurses
// and passing it down every level would buy nothing.
static void (*namfn)(NAMPTR);

//
// Call fn once for every variable, in alphabetical order.  Four things use it: printing
// them (`set'), printing their attributes (`export'), counting them and copying them out
// for a child's environment.
//
void namscan(void (*fn)(NAMPTR))
{
    namfn = fn;
    namwalk(namep);
}

//
// Walk the tree in order -- left subtree, this node, right subtree -- which is what
// visits the names alphabetically.
//
static void namwalk(NAMPTR np)
{
    if (np) {
        namwalk(np->namlft);
        (*namfn)(np);
        namwalk(np->namrgt);
    }
}

//
// Print one variable as `name=value'.  What `set' with no arguments does.  A variable
// with no value is skipped.
//
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

//
// Build "name=value" on the expression stack, in the form a child's environment wants,
// and reserve the space it took.
//
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

//
// Bring one variable's exported copy into step with its value, just before an exec.
//
// A variable has two strings: the value this shell sees, and the copy the environment
// holds.  If it is exported, the environment copy is refreshed from the value; if it is
// not, the value is refreshed from the environment copy -- which is how a variable that
// was inherited but never exported keeps its original value for the child while the
// shell's own change stays private.
//
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

//
// Print one variable's attributes, as `export readonly NAME'.  What `export' and
// `readonly' with no arguments do.  Variables with neither attribute print nothing.
//
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
//
// Read the environment this shell was started with into the name tree, so that $HOME and
// the rest are ordinary variables from then on.  Called once, at startup.
//
void readenv(void)
{
    STRING *e = environ;

    while (*e)
        setname(*e++, N_ENVNAM);
}

// Running count for countnam(), below.
static INT namec;

//
// namscan() helper: count the variables, so that shenv() knows how big a vector to make.
//
void countnam(NAMPTR n)
{
    (void)n;
    namec++;
}

// Where pushnam() is writing, below.
static STRING *argnam;

//
// namscan() helper: copy one variable into the vector shenv() is building.
//
void pushnam(NAMPTR n)
{
    if (n->namval)
        *argnam++ = staknam(n);
}

//
// v7's setenv(): build the environment vector for an exec, on the stack.
//
//
// Build the environment vector to hand to a child: a null-terminated array of
// "name=value" strings, on the expression stack.  Two passes, because the vector has to
// be allocated before it can be filled and only a walk can say how many there are.
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
