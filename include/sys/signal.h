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
// SIGINS, SIGTRC, SIGFPT, SIGKIL, SIGSEG, SIGCLK and SIGTRM.  Those seven are gone
// rather than aliased: the kernel names a signal by the same spelling the user does,
// SIGILL and not SIGINS, so nothing here has two names.  One copy cannot drift from
// itself.
//
// The numbers are v7's, all fifteen of them, and NSIG stays 17: they are the kernel's
// (kernel/sig.c, kernel/machdep.c) and b6sim's (cmd/sim/syscall.cpp), and C11 §7.14
// requires only six of them and permits the rest.  SIGABRT is the one name C11 asks
// for that v7 spelled otherwise -- it is signal 6, which v7 calls SIGIOT -- so it is
// an alias and not a new number.
#ifndef _SYS_SIGNAL_H
#define _SYS_SIGNAL_H

#define NSIG 17

#define SIGHUP  1  // hangup
#define SIGINT  2  // interrupt
#define SIGQUIT 3  // quit
#define SIGILL  4  // illegal instruction (not reset when caught)
#define SIGTRAP 5  // trace trap (not reset when caught)
#define SIGIOT  6  // IOT instruction
#define SIGEMT  7  // EMT instruction
#define SIGFPE  8  // floating point exception
#define SIGKILL 9  // kill (cannot be caught or ignored)
#define SIGBUS  10 // bus error
#define SIGSEGV 11 // segmentation violation
#define SIGSYS  12 // bad argument to system call
#define SIGPIPE 13 // write on a pipe with no one to read it
#define SIGALRM 14 // alarm clock
#define SIGTERM 15 // software termination signal from kill

#define SIGABRT SIGIOT // C11's name for what v7 calls SIGIOT; abort() raises it

#define SIG_DFL ((void (*)(int))0)
#define SIG_IGN ((void (*)(int))1)
#define SIG_ERR ((void (*)(int)) - 1)

#endif // _SYS_SIGNAL_H
