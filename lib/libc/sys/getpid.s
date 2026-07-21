//
// getpid(), getppid() -- this process's id, and its parent's.
//
// One extracode, two entry points: getpid() in kernel/sys4.c sets r_val1 to the pid and
// r_val2 to the parent's, and r_val2 arrives in r12 (b6sim's SYS_getpid does the same).
// The pair has to share a file because it shares the trap, which is also how v7 keeps
// getuid/geteuid and getgid/getegid -- see getuid.s and getgid.s next door.
//
// Neither can fail, so there is no `14 v1m cerror' and errno is left alone.  Neither
// takes an argument, so the gate pops nothing.
//
// getppid() is not in v7's libc at all -- v7 had only the second return value and no
// name for it, which is why fork.s still banks the parent's pid in `par_uid'.  It costs
// one instruction here, so it is offered.
//
        .text
        .globl  getpid, getppid

getpid:
        $77 20                  // SYS_getpid
     13 uj

getppid:
        $77 20                  // SYS_getpid
        ita     12              // A := r_val2, the parent's pid
     13 uj
