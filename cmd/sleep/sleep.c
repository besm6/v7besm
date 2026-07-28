/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// sleep -- suspend execution for an interval.
//
// The second of task C2a's three (../README.md), and the smallest program on the image:
// everything it does beyond parsing one decimal number is libc's sleep(3), which is the
// piece that actually needed a kernel -- alarm(2) to arm SIGALRM, pause(2) to wait for it,
// delivery to get the handler run, and a longjmp out of the signal frame the kernel built
// on the user stack (lib/libc/gen/sleep.c, and lib/test/signals.c is what proves the
// ladder works).  There is nothing machine-dependent left for this file to get wrong: no
// pointer comparison (../README.md §2), no long, no buffer.
//
// ONE DELIBERATE CHANGE.  v7 prints its two diagnostics and then calls exit(0), so a
// script could not tell a rejected sleep from one that really slept; this returns 1.  The
// messages stay on STDOUT, where v7 put them -- that is v7's inconsistency and not this
// port's, as it is in ln and rm (../README.md).  sleep.1 records both.
//
// v7's BUGS section said the interval must be under 65536 seconds.  That was a PDP-11
// fact; the argument to alarm(2) is one 41-bit word here.  sleep.1 records that too.
//
#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    unsigned n;
    int c;
    char *s;

    n = 0;
    if (argc < 2) {
        printf("arg count\n");
        return 1;
    }
    s = argv[1];
    while ((c = *s++) != 0) {
        if (c < '0' || c > '9') {
            printf("bad character\n");
            return 1;
        }
        n = n * 10 + c - '0';
    }
    sleep(n);
    return 0;
}
