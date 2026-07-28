/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// yes -- print a string over and over until killed.
//
//      yes [ string ]
//
// The smallest program on the image, and one of task C2b's five (../README.md).  The C11
// pass is the entire diff: v7 wrote a K&R main with an untyped argc and called printf with
// no #include.
//
// THE LOOP IS THE PROGRAM and is deliberately left as it stands.  yes(1) has no terminating
// condition of its own -- what stops it is the reader at the other end of its pipe going
// away, which costs it a SIGPIPE (kernel/pipe.c raises it beside the EPIPE) and the default
// disposition kills it.  Testing the return value of printf and breaking would change
// nothing on this system, since the signal arrives first and is not caught.
//
// SO IT HAS NO b6sim CASE, and this directory has no test/ at all.  scripts/run-prog-test.sh
// runs a program to completion and diffs a finished file; a program that never completes
// cannot be run there under any argument list.  The assertion is in kernel/test/utils.sh
// instead, where a pipeline can bound it -- `yes hello | { read x; ... }' lets the shell's
// read builtin take one line and exit, and the SIGPIPE that follows is what stops this
// program.  That is also THE FIRST PIPELINE ever run on this image, which utils.sh says at
// the point it does it.
//
// v7 SHIPPED NO MANUAL PAGE for yes -- it is one of the two commands ../README.md names as
// undocumented anywhere in the v7 tree -- so yes.1 beside this file is new.
//
// NOT SETUID: it writes to its own standard output and does nothing else.
//
#include <stdio.h>

int main(int argc, char *argv[])
{
    for (;;)
        printf("%s\n", argc > 1 ? argv[1] : "y");
}
