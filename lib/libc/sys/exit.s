//
// exit(int status), _exit(int status) -- terminate the process.
//
// The status is the single C argument and is already in the accumulator, which is
// where the gate reads the last one; the low 8 bits reach the parent.  Neither name
// returns, so there is no `13 uj' and no stack adjustment: the gate stands in for
// the called function and does the callee's cleanup itself.
//
// The two names share one instruction for now.  Phase 4 gives `exit' a C wrapper
// that runs _cleanup() to flush stdio and then falls through to `_exit', which stays
// this bare trap.
//
        .text
        .globl  exit, _exit
exit:
_exit:
        $77 1                   // SYS_exit
