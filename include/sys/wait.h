// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// wait() and the macros that take its status word apart.  v7 predates this header:
// wait() was declared at the head of every caller, and the status was picked apart
// with hand-written shifts and masks at each site (`(status >> 8) & 0377').  This
// file is that block, written once, under the name POSIX gave it.
//
// The status word is the kernel's, and kernel/sys1.c is the authority:
//
//   - a normal exit stores `(rval & 0377) << 8'      -- low byte zero;
//   - a death by signal stores the signal in the low byte, with 0200 added when a
//     core was dumped (fsig()/xp_xstat, kernel/sys1.c);
//   - a stop under ptrace stores `(sig << 8) | 0177' -- low byte exactly 0177.
//
// So the three cases are told apart by the low byte, which is what the W* macros
// below do.  Each evaluates its argument more than once; they are macros, and the
// argument is always a plain variable at the call sites v7 has.
//
// ONE CAVEAT, AND IT IS THIS MACHINE'S: the status comes back in r12, an index
// register, so it is fifteen bits (lib/libc/sys/wait.S).  A status of
// `(code << 8)' passes 32767 as soon as the exit code passes 127, so exit codes
// 128..255 arrive truncated -- identically under b6sim and under the real kernel.
// Widening it means giving the gate an argument; see lib/README.md.
//
// There is no wait3()/waitpid() and no WNOHANG: this kernel has only the one
// argument-less wait system call (kernel/sysent.c).
#ifndef _SYS_WAIT_H
#define _SYS_WAIT_H

#include <sys/types.h> // pid_t

// Wait for any child; -1 with ECHILD when there are none.  `status' may be a null
// pointer, in which case the status is discarded -- wait.S tests for that.
//
// Not for the kernel side, which has a `void wait(void)' of its own on the other side of
// the gate (<sys/systm.h>, kernel/sys1.c).  The W* macros above are for both.  On the two
// conditions rather than one, see the same guard in <sys/stat.h>.
#if !defined(KERNEL) && !defined(_SYS_SYSTM_H)
pid_t wait(int *status);
#endif

#define WIFEXITED(s)   (((s) & 0377) == 0)      // exited normally
#define WEXITSTATUS(s) (((s) >> 8) & 0377)      // ... with this code
#define WIFSIGNALED(s) (((s) & 0177) != 0 && ((s) & 0377) != 0177)
#define WTERMSIG(s)    ((s) & 0177)             // ... by this signal
#define WCOREDUMP(s)   ((s) & 0200)             // ... and dumped core
#define WIFSTOPPED(s)  (((s) & 0377) == 0177)   // stopped under ptrace()
#define WSTOPSIG(s)    (((s) >> 8) & 0377)      // ... by this signal

#endif // _SYS_WAIT_H
