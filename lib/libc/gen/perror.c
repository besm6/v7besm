// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// Print "s: <the error errno names>" on the standard error.
//
// Straight to fd 2 with write(), not through stdio: that is v7's own arrangement and
// it is also the only one available before phase 4.  errno is the word in
// lib/libc/sys/cerror.s, which is the only thing that ever writes it -- r14 is dead by
// the time any C statement runs, so a stub that forgot its `14 v1m cerror' would still
// hand back -1 and leave this printing the PREVIOUS error.  That is what test/errno is
// for.
//
// The message comes from strerror() (gen/strerror.c) rather than from sys_errlist[] here,
// so the bound test exists once and the two cannot name the same errno differently.
//
#include <errno.h>
#include <string.h>

int write(int fd, const char *buf, int n);

void perror(const char *s)
{
    char *c;
    int n;

    c = strerror(errno);
    n = strlen(s);
    if (n) {
        write(2, s, n);
        write(2, ": ", 2);
    }
    write(2, c, strlen(c));
    write(2, "\n", 1);
}
