//
// dup(int fd), dup2(int fd, int fd2) -- duplicate a descriptor.
//
// v7 hangs both on one syscall: the gate ALWAYS takes two arguments, and bit 0100 of
// the first asks for dup2 with the second naming the descriptor to duplicate onto.
// dup() in kernel/sys3.c reads that two-field argument struct either way, and sysent[41]
// says narg 2 for both -- which is why neither of these can be a generated stub.
//
// The arity is the whole difficulty.  A C `dup(fd)' has ONE argument, so the compiler
// leaves fd in the accumulator and pushes nothing; the gate, expecting two, would read
// its first from an untouched word below r15 and then pop that word, drifting the
// caller's stack by one on every call.  So dup pushes fd itself and passes 0 as the
// unused second argument, and the gate's pop balances the push.
//
// dup2 already has its two arguments in the right places -- fd at r15-1, fd2 in the
// accumulator -- and only has to set the flag bit in the pushed word.  `15 xta -1'
// reads below the stack pointer WITHOUT adjusting it: stack mode is modifier 017 with a
// ZERO offset, which is what makes the bare `15 atx' in dup below a push.
//
// r9 is scratch (only r1-r7 are callee-saved) and fifteen bits are ample for a
// descriptor -- the kernel keeps six, having spent the rest of the field on this flag.
//
        .text
        .globl  dup, dup2, cerror

dup:
     15 atx                     // push fd as the first argument ...
        xta                     // ... and pass 0 as the unused fdes2
        uj      dp_go

dup2:
        ati     9               // r9 := fd2, the last argument
     15 xta     -1              // A := fd, the pushed one
        aox     #0100           // set the dup2 flag the kernel looks for
     15 atx     -1              //   and put it back where the gate reads it
        ita     9               // A := fd2 again

dp_go:
        $77 41                  // SYS_dup
     14 v1m     cerror
     13 uj
