//
// exec(char *name, char **argv) -- replace this process image.
//
// The environment comes from the caller's own, unchanged; exece.s is the form that
// replaces it too.  The C wrappers that build an argv[] -- execl, execv, execle,
// execvp -- are phase 5.
//
// Hand-written because of what it does NOT need: a successful exec never comes back, so
// there is nothing to test r14 for.  Reaching the instruction after the extracode is
// already proof that the call failed, and the branch to cerror is unconditional -- one
// instruction less than the generated stub, and rather more honest about the shape.
//
// The `uj' below is reached because b6as word-aligns after an extracode: a trap returns
// to the left half of the NEXT word, so an instruction packed beside it would never run
// and a failing exec would fall through into whatever object the linker put next.
//
#include <sys/syscall.h>

        .text
        .globl  exec, cerror

exec:
        $77     SYS_exec        // returns only on failure
        uj      cerror
