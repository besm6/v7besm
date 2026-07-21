//
// pid = wait(int *status) -- reap a child; -1 with ECHILD when there are none.
//
// Hand-written for two reasons at once.
//
// The gate takes NO arguments: the status is v7's second result and comes back in r12
// (wait() in kernel/sys1.c, SYS_wait in cmd/sim/syscall.cpp), not through the caller's
// buffer.  So the pointer C hands us never reaches the gate -- this file is what turns
// one calling convention into the other.  Nothing is pushed and nothing is popped.
//
// And the pointer arrives in the accumulator, which the extracode overwrites with the
// pid.  It has to be parked in an index register first.  r9 is free: only r1-r7 are
// callee-saved (doc/Besm6_Calling_Conventions.md).  Fifteen bits are enough for it --
// `status' is an `int *', a PLAIN word address, not a fat char pointer.
//
// A BARE LITERAL IS DECIMAL in b6as, so `ati 9' is r9 and `ati 011' would be too, by a
// different route; the file sticks to decimal throughout, as kernel/switch.s does.
//
// THE STATUS DOES NOT ALWAYS FIT.  r12 is an index register -- 15 bits -- and a wait
// status is (code << 8), which passes 32767 as soon as the exit code passes 127.  Exit
// codes 128..255 therefore arrive truncated, identically here and on the real kernel.
// Widening it means giving the gate an argument and writing the status through this
// pointer kernel-side; see lib/README.md, "Ground rules".
//
#include <sys/syscall.h>

        .text
        .globl  wait, cerror

wait:
        ati     9               // r9 := status, before the trap destroys A
        $77     SYS_wait
     14 v1m     cerror
      9 vzm     wt_dn           // wait(0): the caller does not want the status

     15 atx                     // push the pid -- `ita' below lands on top of it
        ita     12              // A := the status, v7's second result
      9 atx                     // *status = it
     15 xta                     // pop the pid back into A
wt_dn:
     13 uj
