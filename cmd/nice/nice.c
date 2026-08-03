/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// nice -- run a command at a lower priority.
//
//      nice [ -number ] command [ arguments ]
//
// The first of task C8's five (../TODO.md) and the only one of them that asks the kernel
// nothing: no kctl(2), no memory device, no privilege.  It is here first because it proves
// the scaffolding -- the CMake call, the manifest stanza, ROOTFS_FILES, the two hard-coded
// `ls /bin' listings and a b6sim case -- on twenty lines of code.
//
// nice(2) is sysent[] row 34 (kernel/sys4.c): it ADDS to u.u_procp->p_nice and clamps the
// result to 0..2*NZERO-1, NZERO being 20.  A negative increment from anyone but the
// super-user is turned into 0 -- the priority is left alone and suser() reports EPERM.
// v7's nice(1) ignores that failure on purpose, and this one does too: the command still
// runs, at the priority the caller was entitled to.
//
// TWO CHANGES, both of them ../README.md's standard C11 pass:
//
//   strerror() FOR sys_errlist[].  v7 declared `extern char *sys_errlist[]' itself and
//   subscripted it unchecked; no header here declares it, and the subscript is the same
//   latent bug ../kill/kill.c describes -- an errno past the end of the table prints
//   whatever follows it.  strerror() is bounded by sys_nerr (../../lib/libc/gen/errlst.c).
//   THE ARGUMENT ORDER IS v7's and is deliberately kept: the message comes first and the
//   command name second, which reads backwards beside every other program here but is what
//   `nice' has always printed.
//
//   `extern errno' became <errno.h>, and atoi() got <stdlib.h>.
//
// NOT SETUID.  A user lowering their own priority needs nothing; RAISING it wants root, and
// the kernel is where that test belongs and already is.
//
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    int nicarg = 10;

    if (argc > 1 && argv[1][0] == '-') {
        nicarg = atoi(&argv[1][1]);
        argc--;
        argv++;
    }
    if (argc < 2) {
        fputs("usage: nice [ -n ] command\n", stderr);
        return 1;
    }
    nice(nicarg);
    execvp(argv[1], &argv[1]);
    fprintf(stderr, "%s: %s\n", strerror(errno), argv[1]);
    return 1;
}
