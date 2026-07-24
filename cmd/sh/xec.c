/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 1999 Robert Nordier. All rights reserved. */

//
// Command execution -- the tree walker.
//
// EXECUTE() IS v7's ONE VARIADIC FUNCTION.  It is written with four parameters, and v7
// called it with two, three or four: K&R passed whatever was there and the callee read
// what it declared, so the pipe arguments were simply garbage in the calls that did not
// use them (and were never read in those cases).  C11 does not allow it, so the
// twenty-odd pipe-less call sites pass NULL twice.
//
// AND THE FALL-THROUGH FROM `case TCOM' INTO `case TFORK' IS HOW EVERY EXTERNAL COMMAND
// GETS RUN.  TCOM handles a simple command; if it turns out not to be a built-in and
// not to be a bare redirection, control drops into TFORK, which forks and execs it.
// Inside v7's SWITCH/IN/ENDSW macros that was invisible.  It is marked below, with the
// two smaller ones in the built-in switch.
//
#include <signal.h>
#include <sys/stat.h> // umask()
#include <sys/times.h>
#include <sys/wait.h>
#include <unistd.h>

#include "defs.h"
#include "sym.h"

// The pid of the child just forked, or 0 in the child itself.  A file-scope variable
// rather than a local because execute() recurses and v7 wanted the innermost value; it
// is read again as soon as it is set, so the sharing does no harm.
static INT parent;

// ======== command execution ========

//
// Walk one node of the parse tree and do what it says.  THE HEART OF THE SHELL.
//
// It recurses into the children, so running `if a; then b | c; fi' is one call per node.
// The big switch is on the kind of node:
//
//	TCOM		a simple command -- and the one that falls through to TFORK
//	TFORK		run something in a child process
//	TPAR		( ... )
//	TFIL		a | b
//	TLST		a; b
//	TAND, TORF	a && b, a || b
//	TFOR, TWH, TUN	for, while, until
//	TIF		if
//	TSW		case
//
// TCOM does the built-ins itself in an inner switch -- cd, set, export and the rest have
// to run in THIS process, since a child's chdir or assignment would be thrown away when
// it exited.  Anything that is not a built-in falls through into TFORK and is run as a
// separate program.
//
// `execflg' says the caller has no more use for this process, so the command may replace
// it outright instead of forking -- what makes the last stage of a pipeline cheap.  pf1
// and pf2 are the pipes to read from and write to, or NULL.  Returns the exit status.
//
// The stack position is saved on the way in and restored on the way out, so that every
// command hands back the scratch space it used no matter how it left.
//
INT execute(TREPTR argt, INT execflg, INT *pf1, INT *pf2)
{
    // `stakbot' is preserved by this routine
    TREPTR t;
    STKPTR sav = savstak();

    sigchk();

    if ((t = argt) != 0 && execbrk == 0) {
        INT treeflgs;
        INT oldexit, type;
        STRING *com;

        treeflgs = t->tretyp;
        type     = treeflgs & COMMSK;
        oldexit  = exitval;
        exitval  = 0;

        switch (type) {
        case TCOM: {
            STRING a1;
            INT argn, internal;
            ARGPTR schain = gchain;
            IOPTR io      = t->treio;
            gchain        = 0;
            argn          = getarg((COMPTR)t);
            com           = scan(argn);
            a1            = com[1];
            gchain        = schain;

            if ((internal = syslook(com[0], commands)) != 0 || argn == 0)
                setlist(((COMPTR)t)->comset, 0);

            if (argn && (flags & noexec) == 0) {
                // print command if execpr
                if (flags & execpr) {
                    argn = 0;
                    prs(execpmsg);
                    while (com[argn] != ENDARGS) {
                        prs(com[argn++]);
                        blank();
                    }
                    newline();
                }

                switch (internal) {
                case SYSDOT:
                    if (a1) {
                        INT f;

                        if ((f = pathopen(getpath(a1), a1)) < 0)
                            failed(a1, notfound);
                        else
                            execexp(0, f, 0);
                    }
                    break;

                case SYSTIMES: {
                    // v7 read the four times into an L_INT[4] and passed that to
                    // times(), which takes a struct tms.  Same four words, but C11
                    // wants the type it declared.
                    struct tms tb;
                    times(&tb);
                    prt(tb.tms_cutime);
                    blank();
                    prt(tb.tms_cstime);
                    newline();
                } break;

                case SYSEXIT:
                    exitsh(a1 ? stoi(a1) : oldexit);

                case SYSNULL:
                    io = 0;
                    break;

                case SYSCONT:
                    execbrk = -loopcnt;
                    break;

                case SYSBREAK:
                    if ((execbrk = loopcnt) != 0 && a1)
                        breakcnt = stoi(a1);
                    break;

                case SYSTRAP:
                    if (a1) {
                        BOOL clear;
                        if ((clear = digit(*a1)) == 0)
                            ++com;
                        while (*++com) {
                            INT i;
                            if ((i = stoi(*com)) >= MAXTRAP || i < MINTRAP) {
                                failed(*com, badtrap);
                            } else if (clear) {
                                clrsig(i);
                            } else {
                                replace(&trapcom[i], a1);
                                if (*a1)
                                    getsig(i);
                                else
                                    ignsig(i);
                            }
                        }
                    } else {
                        // print out current traps
                        INT i;

                        for (i = 0; i < MAXTRAP; i++) {
                            if (trapcom[i]) {
                                prn(i);
                                prs(colon);
                                prs(trapcom[i]);
                                newline();
                            }
                        }
                    }
                    break;

                case SYSEXEC:
                    com++;
                    initio(io);
                    ioset = 0;
                    io    = 0;
                    if (a1 == 0)
                        break;
                    // FALLTHROUGH -- `exec cmd' is `login'-style: never come back

                case SYSLOGIN:
                    flags |= forked;
                    oldsigs();
                    execa(com);
                    done();

                case SYSCD:
                    if (flags & rshflg)
                        failed(com[0], restricted);
                    else if ((a1 == 0 && (a1 = homenod.namval) == 0) || chdir(a1) < 0)
                        failed(a1, baddir);
                    break;

                case SYSSHFT:
                    if (dolc < 1) {
                        error(badshift);
                    } else {
                        dolv++;
                        dolc--;
                    }
                    assnum(&dolladr, dolc);
                    break;

                case SYSWAIT:
                    await(-1);
                    break;

                case SYSREAD:
                    exitval = readvar(&com[1]);
                    break;

                case SYSSET:
                    if (a1) {
                        INT argc;
                        argc = options(argn, com);
                        if (argc > 1)
                            setargs(com + argn - argc);
                    } else if (((COMPTR)t)->comset == 0) {
                        // scan name chain and print
                        namscan(printnam);
                    }
                    break;

                case SYSRDONLY:
                    exitval = N_RDONLY;
                    // FALLTHROUGH -- readonly and export differ only in the flag bit

                case SYSXPORT:
                    if (exitval == 0)
                        exitval = N_EXPORT;

                    if (a1) {
                        while (*++com)
                            attrib(lookup(*com), exitval);
                    } else {
                        namscan(printflg);
                    }
                    exitval = 0;
                    break;

                case SYSEVAL:
                    if (a1)
                        execexp(a1, 0, &com[2]);
                    break;

                case SYSUMASK:
                    if (a1) {
                        INT c, i;
                        i = 0;
                        while ((c = *a1++) >= '0' && c <= '7')
                            i = (i << 3) + c - '0';
                        umask(i);
                    } else {
                        INT i, j;
                        umask(i = umask(0));
                        prc('0');
                        for (j = 6; j >= 0; j -= 3)
                            prc(((i >> j) & 07) + '0');
                        newline();
                    }
                    break;

                default:
                    internal = builtin(argn, com);
                }

                if (internal) {
                    if (io)
                        error(illegal);
                    chktrap();
                    break;
                }
            } else if (t->treio == 0) {
                break;
            }
        }
            // FALLTHROUGH -- THE IMPORTANT ONE.  A simple command that is not a
            // built-in, and not a bare redirection, is run by forking here.

        case TFORK:
            if (execflg && (treeflgs & (FAMP | FPOU)) == 0) {
                parent = 0;
            } else {
                while ((parent = fork()) == -1) {
                    sigchk();
                    alarm(10);
                    pause();
                }
            }

            if (parent) {
                // This is the parent branch of fork; it may or may not wait for the
                // child.
                if (treeflgs & FPRS && flags & ttyflg) {
                    prn(parent);
                    newline();
                }
                if (treeflgs & FPCL)
                    closepipe(pf1);
                if ((treeflgs & (FAMP | FPOU)) == 0)
                    await(parent);
                else if ((treeflgs & FAMP) == 0)
                    post(parent);
                else
                    assnum(&pcsadr, parent);

                chktrap();
                break;

            } else {
                // this is the forked branch (child) of execute
                flags |= forked;
                iotemp = 0;
                postclr();
                settmp();

                // Turn off INTR and QUIT if `FINT'.  Reset remaining signals to parent
                // except for those `lost' by trap.
                oldsigs();
                if (treeflgs & FINT) {
                    signal(INTR, SIG_IGN);
                    signal(QUIT, SIG_IGN);
                }

                // pipe in or out
                if (treeflgs & FPIN) {
                    shrename(pf1[INPIPE], 0);
                    close(pf1[OTPIPE]);
                }
                if (treeflgs & FPOU) {
                    shrename(pf2[OTPIPE], 1);
                    close(pf2[INPIPE]);
                }

                // default std input for &
                if (treeflgs & FINT && ioset == 0)
                    shrename(chkopen(devnull), 0);

                // io redirection
                initio(t->treio);
                if (type != TCOM)
                    execute(((FORKPTR)t)->forktre, 1, 0, 0);
                else if (com[0] != ENDARGS) {
                    setlist(((COMPTR)t)->comset, N_EXPORT);
                    execa(com);
                }
                done();
            }

        case TPAR:
            shrename(dup(2), output);
            execute(((PARPTR)t)->partre, execflg, 0, 0);
            done();

        case TFIL: {
            INT pv[2];
            chkpipe(pv);
            if (execute(((LSTPTR)t)->lstlef, 0, pf1, pv) == 0)
                execute(((LSTPTR)t)->lstrit, execflg, pv, pf2);
            else
                closepipe(pv);
        } break;

        case TLST:
            execute(((LSTPTR)t)->lstlef, 0, 0, 0);
            execute(((LSTPTR)t)->lstrit, execflg, 0, 0);
            break;

        case TAND:
            if (execute(((LSTPTR)t)->lstlef, 0, 0, 0) == 0)
                execute(((LSTPTR)t)->lstrit, execflg, 0, 0);
            break;

        case TORF:
            if (execute(((LSTPTR)t)->lstlef, 0, 0, 0) != 0)
                execute(((LSTPTR)t)->lstrit, execflg, 0, 0);
            break;

        case TFOR: {
            NAMPTR n = lookup(((FORPTR)t)->fornam);
            STRING *args;
            DOLPTR argsav = 0;

            if (((FORPTR)t)->forlst == 0) {
                args   = dolv + 1;
                argsav = useargs();
            } else {
                ARGPTR schain = gchain;
                gchain        = 0;
                trim((args = scan(getarg(((FORPTR)t)->forlst)))[0]);
                gchain = schain;
            }
            loopcnt++;
            while (*args != ENDARGS && execbrk == 0) {
                assign(n, *args++);
                execute(((FORPTR)t)->fortre, 0, 0, 0);
                if (execbrk < 0)
                    execbrk = 0;
            }
            if (breakcnt)
                breakcnt--;
            execbrk = breakcnt;
            loopcnt--;
            argfor = freeargs(argsav);
        } break;

        case TWH:
        case TUN: {
            INT i = 0;

            loopcnt++;
            while (execbrk == 0 && (execute(((WHPTR)t)->whtre, 0, 0, 0) == 0) == (type == TWH)) {
                i = execute(((WHPTR)t)->dotre, 0, 0, 0);
                if (execbrk < 0)
                    execbrk = 0;
            }
            if (breakcnt)
                breakcnt--;
            execbrk = breakcnt;
            loopcnt--;
            exitval = i;
        } break;

        case TIF:
            if (execute(((IFPTR)t)->iftre, 0, 0, 0) == 0)
                execute(((IFPTR)t)->thtre, execflg, 0, 0);
            else
                execute(((IFPTR)t)->eltre, execflg, 0, 0);
            break;

        case TSW: {
            STRING r = mactrim(((SWPTR)t)->swarg);
            t        = (TREPTR)((SWPTR)t)->swlst;
            while (t) {
                ARGPTR rex = ((REGPTR)t)->regptr;
                while (rex) {
                    STRING s;
                    if (gmatch(r, s = macro(rex->argval)) || (trim(s), eq(r, s))) {
                        execute(((REGPTR)t)->regcom, 0, 0, 0);
                        t = 0;
                        break;
                    } else {
                        rex = ((ARGPTR)rex)->argnxt;
                    }
                }
                if (t)
                    t = (TREPTR)((REGPTR)t)->regnxt;
            }
        } break;
        }
        exitset();
    }

    sigchk();
    tdystak(sav);
    return exitval;
}

//
// Run a string, or a file, as shell input.
//
// v7 declared the second parameter a file descriptor and then handed it a STRING * at
// the `eval' call site, laundering a pointer through an int for feval to pick up again.
// They are separate parameters here: `fd' is a descriptor or 0, `args' is eval's
// argument vector or NULL.
//
//
// Run shell commands from somewhere other than the current input.
//
// From the string `s' -- which is `eval', a trap command, and the `-c' option -- or from
// the already-open descriptor `fd', which is the `.' built-in reading a script.  The
// input is pushed, the commands are parsed and run, and the input is popped, so that
// whatever was being read before carries on untouched.
//
void execexp(STRING s, UFD fd, STRING *args)
{
    SHFILEBLK fb;

    push(&fb);
    if (s) {
        estabf(s);
        fb.feval = args;
    } else if (fd >= 0) {
        initf(fd);
    }
    execute(cmd(NL, NLFLG | MTFLG), 0, 0, 0);
    pop();
}
