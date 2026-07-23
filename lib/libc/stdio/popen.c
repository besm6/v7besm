// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// popen(cmd, mode), pclose(ptr) -- a shell command at the far end of a stream.
//
// One file, as malloc/free/realloc are: the two share popen_pid[], which is how
// pclose() knows whose child to wait for.  v7 sized that table 20 by hand; here it is
// _NFILE, the same bound findiop() puts on descriptors, since it is indexed by one.
//
// Two changes from v7, both forced by <signal.h>: a disposition is `void (*)(int)',
// and the three saved ones are declared as such rather than as v7's `(*hstat)()'.
//
// The wait status is wait()'s raw one and rides back in r12, so an exit code above 127
// arrives truncated -- see stdio/system.c and lib/README.md.  The child ends with
// _exit(), which is the bare trap: exit() would flush the stdio buffers it inherited
// from the parent and print the parent's pending output twice.
//
// One deviation: a fork() that fails closes both ends of the pipe before returning
// NULL.  v7 leaked them, and a caller that retries would run out of descriptors long
// before it ran out of reasons to retry.
//
#include <signal.h>
#include <stdio.h>

int pipe(int *fildes);
int fork(void);
int close(int fd);
int dup2(int fd, int fd2);
int execl(const char *name, ...);
int wait(int *status);
_Noreturn void _exit(int status);

#define tst(a, b) (*mode == 'r' ? (b) : (a))
#define RDR       0
#define WTR       1

static int popen_pid[_NFILE];

FILE *popen(const char *cmd, const char *mode)
{
    int p[2];
    int myside, hisside, pid;

    if (pipe(p) < 0)
        return NULL;
    myside  = tst(p[WTR], p[RDR]);
    hisside = tst(p[RDR], p[WTR]);
    if ((pid = fork()) == 0) {
        // myside and hisside reverse roles in the child
        close(myside);
        dup2(hisside, tst(0, 1));
        close(hisside);
        execl("/bin/sh", "sh", "-c", cmd, (char *)0);
        _exit(1);
    }
    if (pid == -1) {
        close(myside);
        close(hisside);
        return NULL;
    }
    popen_pid[myside] = pid;
    close(hisside);
    return fdopen(myside, mode);
}

int pclose(FILE *ptr)
{
    int f, r, status;
    void (*hstat)(int);
    void (*istat)(int);
    void (*qstat)(int);

    f = fileno(ptr);
    fclose(ptr);
    istat = signal(SIGINT, SIG_IGN);
    qstat = signal(SIGQUIT, SIG_IGN);
    hstat = signal(SIGHUP, SIG_IGN);
    while ((r = wait(&status)) != popen_pid[f] && r != -1)
        ;
    if (r == -1)
        status = -1;
    signal(SIGINT, istat);
    signal(SIGQUIT, qstat);
    signal(SIGHUP, hstat);
    return status;
}
