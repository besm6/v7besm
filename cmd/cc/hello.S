//
// hello.S -- the demo program.
//
//      $ cc hello.S
//      $ ./a.out
//      Hello BESM-6!
//
#include <sys/syscall.h>

        .text
        .globl  main
main:
        xta     #1          // arg 1: file descriptor 1, the standard output
        xts     msgptr      // arg 2: pushes arg 1, A := the message pointer
        xts     #MSGLEN     // arg 3, the last, so it stays in A; pushes arg 2
        $77     SYS_write   // write(1, msgptr, MSGLEN)

        xta     #0          // exit's one argument is the status
        $77     SYS_exit    //   ...and this does not return

        .data
msgptr: .word  0'64 + message       // fat pointer
message: .ascii "Hello BESM-6!\n"   // message contents
MSGLEN = 14                         // length in bytes
