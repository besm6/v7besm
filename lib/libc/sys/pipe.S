//
// pipe(int fildes[2]) -- 0 on success, -1 on failure.
//
// Hand-written for the same reason as wait.s: the gate takes NO arguments and returns
// BOTH descriptors in registers -- the read end in the accumulator, the write end in
// r12 (pipe() in kernel/pipe.c, SYS_pipe in cmd/sim/syscall.cpp).  The caller's array
// is filled here, not by the kernel, so nothing is pushed and nothing is popped.
//
// `fildes' is an `int *', a plain word address, so r9 holds it exactly; and it arrives
// in the accumulator, which the extracode overwrites, so it is parked before the trap.
// r9 is scratch -- only r1-r7 are callee-saved.
//
#include <sys/syscall.h>

        .text
        .globl  pipe, cerror

pipe:
        ati     9               // r9 := fildes, before the trap destroys A
        $77     SYS_pipe
     14 v1m     cerror

      9 atx                     // fildes[0] = the read end, already in A
        ita     12
      9 atx     1               // fildes[1] = the write end, v7's second result
        xta                     // return 0 (memory word 0 is architecturally zero)
     13 uj
