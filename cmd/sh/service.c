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

static STRING xecmsg;
static STRING *xecenv;

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

// for processes to be waited for
#define MAXP 20
static INT pwlist[MAXP];
static INT pwc;

void postclr(void)
{
    INT *pw = pwlist;

    while (pw <= &pwlist[pwc])
        *pw++ = 0;
    pwc = 0;
}

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
// Strip the quoting bit from a string, recording in `nosubst' whether any was set.
//
void trim(STRING at)
{
    STRING p;
    CHAR c;
    CHAR q = 0;

    if ((p = at) != 0) {
        while ((c = *p) != 0) {
            *p++ = c & STRIP;
            q |= c;
        }
    }
    nosubst = q & QUOTE;
}

STRING mactrim(STRING s)
{
    STRING t = macro(s);
    trim(t);
    return t;
}

//
// Turn the gchain into an argv, sorting each marked group.
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

static INT split(STRING s)
{
    STRING argp;
    INT c;
    INT count = 0;

    for (;;) {
        sigchk();
        argp = locstak() + BYTESPERWORD;
        while ((c = *s++, !any(c, ifsnod.namval) && c)) {
            chkstak(argp);
            *argp++ = c;
        }
        if (argp == staktop + BYTESPERWORD) {
            if (c)
                continue;
            else
                return count;
        } else if (c == 0) {
            s--;
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
