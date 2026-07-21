//
// pid = fork() -- 0 in the child, the child's pid in the parent, -1 on failure.
//
// Hand-written because the gate does not answer the question C asks.  Each side gets
// the OTHER side's pid in the accumulator -- distinct, but not self-identifying -- and
// it is the SECOND result, r12, that says which side you are: 1 in the child, 0 in the
// parent.  Both implementations agree and say so at length: fork() in kernel/sys1.c
// and SYS_fork in cmd/sim/syscall.cpp.
//
// So the child's accumulator holds its parent's pid, which C's fork() has nowhere to
// put -- it must return 0.  v7 banks it in `par_uid' rather than throw it away, and
// this does the same; the name is v7's own (lib/tmp/libc/sys/fork.s) and predates
// getppid(), which is now available and is the better way to ask.
//
// fork takes no arguments, so the gate pops nothing and r15 is untouched throughout.
//
#include <sys/syscall.h>

        .text
        .globl  fork, par_uid, cerror

fork:
        $77     SYS_fork
     14 v1m     cerror
     12 vzm     fk_p            // r12 == 0: the parent, and A is already the child's pid
        atx     <par_uid>       // the child: bank the parent's pid ...
        xta                     // ... and return 0.  A bare `xta' reads memory word 0,
                                //   which is architecturally zero.
fk_p:
     13 uj

        .bss
par_uid:
        . = . + 1
