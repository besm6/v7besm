// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// <signal.h> -- signal handling (C11 §7.14).
//
// The numbers, NSIG and the three dispositions are in <sys/signal.h>, their one
// home, which the kernel reads too; this file adds what only the user side wants.
//
// The TYPES are C11's, not v7's, and that is the change here.  v7 declared
// `int (*signal())();' -- no prototype at all, a handler returning int, and a
// SIG_DFL cast to that shape.  It was already unusable: lib/libc/gen/sleep.c
// included this header and then re-declared signal itself.  A handler now takes
// the signal number and returns void, per §7.14.1.1.
//
// That commits the kernel.  sendsig() in kernel/machdep.c pushes a single word
// and jumps, and does not tell the handler which signal it is handling; the
// signal frame is still to be designed (lib phase 6), and it now
// has to carry the number as the handler's argument.  Nothing in libc breaks in
// the meantime: the signal() stub is generated assembly and has no opinion about
// C types, and b6sim answers anything but SIG_DFL/SIG_IGN with EINVAL.
#ifndef _SIGNAL_H
#define _SIGNAL_H

#include <sys/signal.h> // NSIG, the signal numbers, SIG_DFL/SIG_IGN/SIG_ERR
#include <sys/types.h>  // pid_t, for kill()

// §7.14: an object that can be written from a handler without a data race.  One
// word, like everything else, and there is nothing finer to choose.
typedef int sig_atomic_t;

void (*signal(int sig, void (*func)(int)))(int);

// Send a signal.  v7's kill() is POSIX's, negative pids included: pid 0 signals
// every process in the sender's process group, and -1 every process the sender
// may signal (kernel/sig.c).  /etc/init leans on that -1 at shutdown.
int kill(pid_t pid, int sig);

// ---- declared for future implementation: lib phase 6 (TODO) ----
// raise(sig) is kill(getpid(), sig); it waits on delivery, like everything else
// that would have to observe a handler actually running.
int raise(int sig);

#endif // _SIGNAL_H
