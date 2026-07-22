// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// <signal.h> -- signal handling (C11 §7.14).
//
// The numbers are v7's, all fifteen of them, and NSIG stays 17: they are the
// kernel's (kernel/sig.c, kernel/machdep.c) and b6sim's (cmd/sim/syscall.cpp),
// and C11 §7.14 requires only six of them and permits the rest.  SIGABRT is the
// one name C11 asks for that v7 spelled otherwise -- it is signal 6, which v7
// calls SIGIOT -- so it is an alias and not a new number.
//
// The TYPES are C11's, not v7's, and that is the change here.  v7 declared
// `int (*signal())();' -- no prototype at all, a handler returning int, and a
// SIG_DFL cast to that shape.  It was already unusable: lib/libc/gen/sleep.c
// included this header and then re-declared signal itself.  A handler now takes
// the signal number and returns void, per §7.14.1.1.
//
// That commits the kernel.  sendsig() in kernel/machdep.c pushes a single word
// and jumps, and does not tell the handler which signal it is handling; the
// signal frame is still to be designed (lib phase 6, kernel/TODO.md), and it now
// has to carry the number as the handler's argument.  Nothing in libc breaks in
// the meantime: the signal() stub is generated assembly and has no opinion about
// C types, and b6sim answers anything but SIG_DFL/SIG_IGN with EINVAL.
#ifndef _SIGNAL_H
#define _SIGNAL_H

// §7.14: an object that can be written from a handler without a data race.  One
// word, like everything else, and there is nothing finer to choose.
typedef int sig_atomic_t;

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

void (*signal(int sig, void (*func)(int)))(int);

// ---- declared for future implementation: lib phase 6 (TODO) ----
// raise(sig) is kill(getpid(), sig); it waits on delivery, like everything else
// that would have to observe a handler actually running.
int raise(int sig);

#endif // _SIGNAL_H
