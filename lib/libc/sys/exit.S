//
// _exit(int status) -- terminate the process, flushing nothing.
//
// The status is the single C argument and is already in the accumulator, which is
// where the gate reads the last one; the low 8 bits reach the parent.  It does not
// return, so there is no `13 uj' and no stack adjustment: the gate stands in for
// the called function and does the callee's cleanup itself.
//
// There is no `14 v1m cerror' either, for the same reason: the gate has no way to
// report a failure to a caller it never returns to.  This is one of the few stubs the
// generator cannot produce (sys/syscalls.tbl).
//
// `exit' is NOT here.  It is the C wrapper in gen/cuexit.c that runs _cleanup() to
// flush stdio and then calls this; crt0.s jumps to that one.
//
#include <sys/syscall.h>

        .text
        .globl  _exit
_exit:
        $77     SYS_exit
