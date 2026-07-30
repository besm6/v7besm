// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// Signal numbers -- the numbering, and its one home.
//
// Both sides of the KERNEL gate read this file: the kernel posts these with
// psignal() and stores dispositions in u.u_signal[NSIG] (kernel/sig.c), while
// <signal.h> adds sig_atomic_t and the prototypes C11 wants and leaves the numbers
// to this file.  It is #define-only for that reason, so that including it commits a
// translation unit to nothing at all.
//
// v7 wrote the list out twice, in <signal.h> and in <sys/param.h>, and this port
// inherited both.  They are gone, on <sys/errno.h>'s precedent: nine of the macros
// were literal duplicates that survived only because b6cpp compares replacement text
// and the two copies happened to agree character for character, and the other seven
// were a second NAME for a number that already had one -- v7's kernel spellings
// SIGINS, SIGTRC, SIGFPT, SIGKIL, SIGSEG, SIGCLK and SIGTRM, and with them SIGIOT,
// which was signal 6 under an alias named SIGABRT.  All eight are gone rather than
// aliased: every signal here has exactly ONE name, the kernel spells it the way the
// user does -- SIGILL and not SIGINS, SIGABRT and not SIGIOT -- and one copy cannot
// drift from itself.
//
// The numbers are v7's, all fifteen of them, and NSIG stays 17: they are the kernel's
// (kernel/sig.c, kernel/machdep.c) and b6sim's (cmd/sim/syscall.cpp), and C11 §7.14
// requires only six of them and permits the rest.  Signal 6 is the one C11 spells
// otherwise than v7 did, and C11's name is the one kept: v7 called it SIGIOT after the
// PDP-11 IOT instruction, which this machine has not got, while SIGABRT is what §7.14
// requires and what abort() raises.  The NUMBER is still v7's.
#ifndef _SYS_SIGNAL_H
#define _SYS_SIGNAL_H

#define NSIG 17

#define SIGHUP  1  // hangup
#define SIGINT  2  // interrupt
#define SIGQUIT 3  // quit
#define SIGILL  4  // illegal instruction (not reset when caught)
#define SIGTRAP 5  // trace trap (not reset when caught)
#define SIGABRT 6  // abort() -- v7's SIGIOT, the PDP-11 IOT instruction
#define SIGEMT  7  // EMT instruction
#define SIGFPE  8  // floating point exception
#define SIGKILL 9  // kill (cannot be caught or ignored)
#define SIGBUS  10 // bus error
#define SIGSEGV 11 // segmentation violation
#define SIGSYS  12 // bad argument to system call
#define SIGPIPE 13 // write on a pipe with no one to read it
#define SIGALRM 14 // alarm clock
#define SIGTERM 15 // software termination signal from kill

#define SIG_DFL ((void (*)(int))0)
#define SIG_IGN ((void (*)(int))1)
#define SIG_ERR ((void (*)(int)) - 1)

#endif // _SYS_SIGNAL_H
