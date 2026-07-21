//
// write(int fd, char *buf, int n) -- the v7 syscall, as a bare extracode leaf.
//
// A stub has no prologue: the C calling convention has already put fd and buf just
// below r15 and left n, the last argument, in the accumulator, which is exactly
// where the gate reads them (kernel/syscall.c, cmd/sim/syscall.cpp).  A b$save here
// would move them.  It must not pop the stack either -- the gate performs the
// callee's cleanup, `tr->r15 -= n - 1', on the stub's behalf.
//
// buf is a fat pointer and is passed through untouched; the count is in bytes.  The
// result comes back in the accumulator, errno in r14.  Testing r14 and branching to
// cerror is phase 1; until then a failing write returns -1 and says nothing about why.
//
        .text
        .globl  write
write:
        $77 4                   // SYS_write
     13 uj                      // return through r13
