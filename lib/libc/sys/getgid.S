//
// getgid(), getegid() -- the real group id, and the effective one.
//
// One extracode, two entry points; see getuid.S, which this mirrors exactly.  getgid()
// in kernel/sys4.c sets r_val1 to u_rgid and r_val2 to u_gid, and r_val2 arrives in r12.
//
#include <sys/syscall.h>

        .text
        .globl  getgid, getegid

getgid:
        $77     SYS_getgid
     13 uj

getegid:
        $77     SYS_getgid
        ita     12              // A := r_val2, the effective gid
     13 uj
