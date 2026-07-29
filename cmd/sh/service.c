/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 1999 Robert Nordier. All rights reserved. */

//
// Service routines for `execute': redirection, path search, argument generation and
// waiting for children.
//
#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "defs.h"

// Defined below.
static STRING execs(STRING ap, STRING t[]);
static void gsort(STRING from[], STRING to[]);
static INT split(STRING s);

//
// ARGMK marks the last argument of a group on the gchain, so that scan() knows where to
// stop sorting.  v7 packed it into BIT 0 of the chain pointer, which is free on a
// byte-addressed machine and is a significant ADDRESS BIT here -- setting it would name
// the neighbouring word.  It moves to bit 16, exactly as blok.c's BUSY bit does and for
// the same reason; lib/libc/gen/malloc.c carries the full account.
//
#define ARGMK 0100000

#define argmark(p)   ((ARGPTR)((POS)(p) | ARGMK))
#define argmarked(p) ((POS)(p) & ARGMK)
#define argclear(p)  ((ARGPTR)((POS)(p) & ~(POS)ARGMK))

//
// v7 declared `INT errno;' here -- a definition, not a reference, which is how a source
// with no <errno.h> got at the value.  libc defines errno (lib/libc/sys/cerror.s) and
// <errno.h> makes the name a macro over it, so the declaration and the five error
// numbers v7 spelled out below it are all gone.
//

// ======== service routines for `execute' ========

//
// Perform a command's redirections: open each file and move the descriptor into place.
//
// Runs down the chain the parser built, doing them in order -- which is why `>f 2>&1'
// and `2>&1 >f' mean different things.  The kinds are: a here-document (IODOC, whose
// text is already in a temp file), a descriptor move as in `2>&1' (IOMOV), an append
// (IOAPP), a plain `>' and a plain `<'.
//
void initio(IOPTR iop)
{
    STRING ion;
    INT iof, fd;

    if (iop) {
        iof = iop->iofile;
        ion = mactrim(iop->ioname);
        if (*ion && (flags & noexec) == 0) {
            if (iof & IODOC) {
                subst(chkopen(ion), (fd = tmpfil()));
                close(fd);
                fd = chkopen(tmpout);
                unlink(tmpout);
            } else if (iof & IOMOV) {
                if (eq(minus, ion)) {
                    fd = -1;
                    close(iof & IOUFD);
                } else if ((fd = stoi(ion)) >= USERIO) {
                    failed(ion, badfile);
                } else {
                    fd = dup(fd);
                }
            } else if ((iof & IOPUT) == 0) {
                fd = chkopen(ion);
            } else if (flags & rshflg) {
                failed(ion, restricted);
            } else if (iof & IOAPP && (fd = open(ion, O_WRONLY)) >= 0) {
                lseek(fd, (off_t)0, SEEK_END);
            } else {
                fd = create(ion);
            }
            if (fd >= 0)
                shrename(fd, iof & IOUFD);
        }
        initio(iop->ionxt);
    }
}

//
// Which directories to look for a command in.
//
// A name containing a slash is used as written, so the answer is the empty path.
// Otherwise it is PATH, or a fixed default if PATH is unset.  A restricted shell refuses
// slashed names outright, which is what stops the user escaping it by naming /bin/sh.
//
STRING getpath(STRING s)
{
    STRING path;

    if (any('/', s)) {
        if (flags & rshflg)
            failed(s, restricted);
        else
            return nullstr;
    } else if ((path = pathnod.namval) == 0) {
        return defpath;
    } else {
        return cpystak(path);
    }
    return nullstr; // not reached: failed() does not return
}

//
// Try to open `name' in each directory of `path' in turn, and return the first that
// works, or -1.  Used by the `.' built-in to find a script.
//
INT pathopen(STRING path, STRING name)
{
    UFD f;

    do {
        path = catpath(path, name);
    } while ((f = open(curstak(), O_RDONLY)) < 0 && path);
    return f;
}

//
// Join one PATH element to a name, leaving the result on top of the stack, and return
// what is left of the path.
//
STRING catpath(STRING path, STRING name)
{
    STRING scanp = path;
    STRING argp  = locstak();

    while (*scanp && *scanp != COLON) {
        chkstak(argp);
        *argp++ = *scanp++;
    }
    if (scanp != path) {
        chkstak(argp);
        *argp++ = '/';
    }
    if (*scanp == COLON)
        scanp++;
    path  = (*scanp ? scanp : 0);
    scanp = name;
    do {
        chkstak(argp);
    } while ((*argp++ = *scanp++) != 0);
    return path;
}

// The message to report if the search below runs out of directories: "not found"
// normally, but "cannot execute" once some directory has produced a file that existed
// and simply would not run -- which is the more useful complaint of the two.
static STRING xecmsg;

// The environment vector for the exec, built once before the search rather than at each
// candidate.
static STRING *xecenv;

//
// Run an external command: search PATH for it and exec it.
//
// Only returns if nothing could be run, and then only to report why.  A successful exec
// replaces this process, so there is nothing to come back to.
//
void execa(STRING at[])
{
    STRING path;
    STRING *t = at;

    if ((flags & noexec) == 0) {
        xecmsg = notfound;
        path   = getpath(*t);
        namscan(exname);
        xecenv = shenv();
        while ((path = execs(path, t)) != 0)
            ;
        failed(*t, xecmsg);
    }
}

//
// Try ONE directory of the path: build the full name and exec it.
//
// Returns the rest of the path to keep searching with, so execa()'s loop walks the whole
// of PATH by calling this until it returns null.  It returns at all only when the exec
// failed, and what it does then depends on why: a file that is not a binary is assumed
// to be a SHELL SCRIPT and is read by this shell instead (the ENOEXEC case, which jumps
// back to the top of main), an outright error is reported, and a plain "no such file"
// simply moves on to the next directory.
//
static STRING execs(STRING ap, STRING t[])
{
    STRING p, prefix;

    prefix = catpath(ap, t[0]);
    trim(p = curstak());

    sigchk();

    // EXECE, NOT EXECVE: this tree's libc spells the three-argument exec with v7's own
    // name (include/unistd.h, lib/libc/sys/exece.S) and has no symbol called execve.
    exece(p, &t[0], xecenv);

    switch (errno) {
    case ENOEXEC:
        // Not a binary: re-read it as a shell script in this process.
        flags  = 0;
        comdiv = 0;
        ioset  = 0;
        clearup(); // remove open files and for loop junk
        if (input)
            close(input);
        close(output);
        output = 2;
        input  = chkopen(p);

        // set up new args
        setargs(t);
        longjmp(subshell, 1);
        break;

    case ENOMEM:
        failed(p, toobig);

    case E2BIG:
        failed(p, arglist);

    case ETXTBSY:
        failed(p, txtbsy);

    default:
        xecmsg = badexec;
        // FALLTHROUGH -- an unrecognised failure keeps searching the path, like ENOENT

    case ENOENT:
        return prefix;
    }

    // Not reached: every arm above either returns or does not come back.  Said out loud
    // because longjmp() is deliberately not _Noreturn in this tree's <setjmp.h>.
    return 0;
}

// The children this shell still has to wait for -- background commands, and the members
// of a pipeline.  A fixed twenty; past that the oldest is forgotten rather than the list
// overrun.
#define MAXP 20
static INT pwlist[MAXP]; // their process ids, 0 for an empty slot
static INT pwc;          // how many are in the list

//
// Forget every child.  A newly forked child does this at once: the processes its parent
// was waiting for are not its business.
//
void postclr(void)
{
    INT *pw = pwlist;

    while (pw <= &pwlist[pwc])
        *pw++ = 0;
    pwc = 0;
}

//
// Remember one more child to wait for.
//
void post(INT pcsid)
{
    INT *pw = pwlist;

    if (pcsid) {
        while (*pw)
            pw++;
        if (pwc >= MAXP - 1)
            pw--;
        else
            pwc++;
        *pw = pcsid;
    }
}

//
// Wait for child `i' -- or, when i is -1, for all of them, which is the `wait' built-in.
//
// Reports any child that died on a signal ("Killed", "Quit", ...) unless the signal was
// one the user obviously caused, such as an interrupt.  Sets $? from the result: the
// child's own exit status normally, or 0200 plus the signal number if it was killed.
//
void await(INT i)
{
    INT rc = 0, wx = 0;
    INT w;
    INT ipwc = pwc;

    post(i);
    while (pwc) {
        INT p;
        INT sig;
        INT w_hi;

        {
            INT *pw = pwlist;
            p       = wait(&w);
            while (pw <= &pwlist[ipwc]) {
                if (*pw == p) {
                    *pw = 0;
                    pwc--;
                } else {
                    pw++;
                }
            }
        }

        if (p == -1)
            continue;

        //
        // The wait status comes back through r12, an index register, so it is fifteen
        // bits wide and an exit code of 128 or more arrives truncated
        // (lib/libc/sys/wait.S says so).  That is the kernel ABI, not something this
        // file can repair, and it bites the shell twice over: rc below is built as
        // sig|SIGFLG == 0200+sig, which is itself in the truncated range.
        //
        w_hi = (w >> 8) & LOBYTE;

        if ((sig = w & 0177) != 0) {
            if (sig == 0177) { // ptrace! return
                prs("ptrace: ");
                sig = w_hi;
            }
            // v7 indexed sysmsg[] with an unchecked signal number; the table has
            // MAXTRAP entries and nothing bounded the subscript.
            if (sig < MAXTRAP && sysmsg[sig]) {
                if (i != p || (flags & prompt) == 0) {
                    prp();
                    prn(p);
                    blank();
                }
                prs(sysmsg[sig]);
                if (w & 0200)
                    prs(coredump);
            }
            newline();
        }

        if (rc == 0)
            rc = (sig ? sig | SIGFLG : w_hi);
        wx |= w;
    }

    if (wx && flags & errflg)
        exitsh(rc);
    exitval = rc;
    exitset();
}

//
// Take the quoting marks off a string, now that quoting has done its work, and record in
// `nosubst' whether there were any.
//
// THIS IS THE ONE DECODER ON THE WAY OUT -- defs.h's first invariant.  Everything above it
// works in the stored form; below it are exece(), open(), stat() and the user's eyes, and
// they get the bytes that were written.  It compacts IN PLACE, which is safe on two
// counts: the decoded text is never longer than the encoded text, and no parse-tree word
// is ever passed here -- only stack items that macro(), split() or scan() have just built,
// which is what lets a `for' loop expand the same word again on the next pass.
//
// nosubst counts REAL marks.  v7 set it from the OR of every character against 0200, and
// one Cyrillic letter in a here-document terminator would have turned substitution off in
// the body; a QESC QESC pair is a literal 0377 and deliberately does not count.
//
void trim(STRING at)
{
    STRING p, q;
    BOOL quoted = 0;
    INT c;

    if ((p = q = at) != 0) {
        while (*q) {
            // A QESC QESC pair is a literal 0377 and not a quoted character; a QESC with
            // nothing after it is the empty quoted word, and that one does count.
            if (*q == QESC && q[1] != QESC)
                quoted++;
            if ((c = nextq(&q)) == 0)
                break;
            *p++ = c;
        }
        *p = 0;
    }
    nosubst = quoted;
}

//
// macro() then trim(): expand a word and then take the quote marks off it.  The usual
// pair, for a word about to be used as a file name.
//
STRING mactrim(STRING s)
{
    STRING t = macro(s);
    trim(t);
    return t;
}

//
// Turn the gchain into an argv, sorting each marked group.
//
//
// Turn the chain of argument words into the argv vector a child gets.
//
// The chain is in reverse order, so the vector is filled from the end backwards.  Each
// group of words that came from one wildcard -- the groups the ARGMK bit marks -- is
// sorted as it is passed, which is why `echo *' comes out in alphabetical order.
//
STRING *scan(INT argn)
{
    ARGPTR argp = argclear(gchain);
    STRING *comargn, *comargm;

    comargn  = (STRING *)getstak(BYTESPERWORD * argn + BYTESPERWORD);
    comargm  = comargn += argn;
    *comargn = ENDARGS;

    while (argp) {
        *--comargn = argp->argval;
        //
        // THE FIRST WORD IS NOT TRIMMED, and that is v7's, not an oversight of this task:
        // the chain runs backwards, so the last iteration is com[0], and the test below
        // skips it.  What it means is that a command NAME with a quoted character in it
        // reaches the child as argv[0] in the stored form -- execs() trims the path it
        // actually execs (above), so the program still runs and only its own idea of its
        // name is odd.  Trimming here would fix that and would also make `"cd"' reach the
        // built-in table, which is a change to what the shell does; xec.c patches the same
        // quirk by hand for `for'.
        //
        if ((argp = argp->argnxt) != 0)
            trim(*comargn);
        if (argp == 0 || argmarked(argp)) {
            gsort(comargn, comargm);
            comargm = comargn;
        }
        argp = argclear(argp);
    }
    return comargn;
}

//
// Sort a run of strings into alphabetical order.  A shell sort: cheap to write, no extra
// memory, and quite good enough for the number of files one wildcard matches.
//
static void gsort(STRING from[], STRING to[])
{
    INT k, m, n;
    INT i, j;

    if ((n = to - from) <= 1)
        return;

    for (j = 1; j <= n; j *= 2)
        ;

    for (m = 2 * j - 1; (m /= 2) != 0;) {
        k = n - m;
        for (j = 0; j < k; j++) {
            for (i = j; i >= 0; i -= m) {
                STRING *fromi;
                fromi = &from[i];
                if (cf(fromi[m], fromi[0]) > 0) {
                    break;
                } else {
                    STRING s;
                    s        = fromi[m];
                    fromi[m] = fromi[0];
                    fromi[0] = s;
                }
            }
        }
    }
}

// ======== Argument list generation ========

//
// Expand a command's arguments and return how many words came out.
//
// Each word written on the command line goes through macro() for $ substitution and then
// split(), which may turn it into several words or none.
//
INT getarg(COMPTR ac)
{
    ARGPTR argp;
    INT count = 0;
    COMPTR c;

    if ((c = ac) != 0) {
        argp = c->comarg;
        while (argp) {
            count += split(macro(argp->argval));
            argp = argp->argnxt;
        }
    }
    return count;
}

//
// Split an already-expanded string into words at the IFS characters, and expand each
// word against the file system.
//
// This is the second half of what happens to `$x': the value is substituted first, and
// only THEN split, which is why an unquoted variable holding "a b" becomes two
// arguments and a quoted one stays as a single argument.  Empty pieces are dropped.
//
static INT split(STRING s)
{
    STRING argp;
    INT c;
    INT count = 0;

    for (;;) {
        BOOL atend;

        sigchk();
        argp = locstak() + BYTESPERWORD;
        //
        // The word is copied in the STORED FORM, pair and all: this routine only decides
        // where the boundaries are, and expand() and trim() still have to read what it
        // leaves.  Which is why the IFS test is put to the DECODED character -- a quoted
        // space carries the mark and so is not a separator, and that is the mechanism by
        // which "$x" stays one word.
        //
        // `atend' replaces v7's `s--'.  It stopped by backing the cursor over the terminator
        // so that the next round would meet it again; the cursor is simply LEFT on the
        // terminator here, which a variable-width encoding can do and a `--' cannot.
        //
        atend = 1;
        while (*s) {
            if (*s == QESC && s[1] == 0) {
                //
                // "" -- the empty quoted word.  It has to reach argv as an empty argument
                // rather than as no argument at all, so the lone QESC is carried through
                // as it stands and trim() is what finally makes it empty.
                //
                chkstak(argp);
                *argp++ = QESC;
                s++;
                break;
            }
            c = nextq(&s);
            if (any(c, ifsnod.namval)) {
                atend = 0;
                break;
            }
            argp = putq(argp, c);
        }
        if (argp == staktop + BYTESPERWORD) {
            if (!atend)
                continue;
            else
                return count;
        }
        if ((c = expand(((ARGPTR)(argp = endstak(argp)))->argval, 0)) != 0) {
            count += c;
        } else {
            makearg(argp);
            count++;
        }
        gchain = argmark(gchain);
    }
}
