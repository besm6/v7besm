/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// kill - send signal to process
//
// The third of task C2a's three (../README.md).  Nothing here is machine-dependent: no
// pointer comparison (§2), no long, no %D, no buffer of its own -- it walks argv and
// calls kill(2), which the kernel has had since signal delivery landed.
//
// ONE FIX.  v7 printed sys_errlist[errno] with the subscript unchecked, which is the same
// latent bug cmd/sh/service.c had to fix in its sysmsg[] table: an errno the table does
// not reach indexes off the end of it and prints whatever follows.  strerror() is bounded
// by sys_nerr (lib/libc/gen/errlst.c) and is what this libc provides, so it is used
// instead and the extern declarations go with it.  v7's `%u' for a pid became `%d'; a
// pid is an int and never negative here.
//
// The diagnostics stay on STDOUT, where v7 put them, for the reason ln and rm keep theirs
// there (../README.md).
//
// NOT SETUID: kill(2) skips any process whose uid differs from the sender's unless the
// sender is the super-user (kill() in kernel/sys4.c), and that gate is what stops one user
// signalling another's processes.  A pid that matched nothing comes back ESRCH.
//
// The C11 pass: a prototype and an explicit return type on main, `int' on the three
// variables v7 declared `register' with no type at all, <errno.h> instead of `extern
// errno', <stdlib.h> for atoi(), and v7's `goto usage' into a label inside an if-block
// replaced by a helper -- the jump was legal, labels having function scope, but nothing
// about the source read that way.
//
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void)
{
    printf("usage: kill [ -signo ] pid ...\n");
}

int main(int argc, char *argv[])
{
    int signo, pid, res;
    int errlev = 0;

    if (argc <= 1) {
        usage();
        return 2;
    }
    if (*argv[1] == '-') {
        signo = atoi(argv[1] + 1);
        argc--;
        argv++;
    } else {
        signo = SIGTERM;
    }
    argv++;
    while (argc > 1) {
        if (**argv < '0' || **argv > '9') {
            usage();
            return 2;
        }
        pid = atoi(*argv);
        res = kill(pid, signo);
        if (res < 0) {
            printf("%d: %s\n", pid, strerror(errno));
            errlev = 1;
        }
        argc--;
        argv++;
    }
    return errlev;
}
