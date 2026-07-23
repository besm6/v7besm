/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * system(s) -- run one command line through the shell and hand back its wait status.
 *
 * v7's, and the only thing that changed is the type of the two saved dispositions: a
 * handler is `void (*)(int)' now (<signal.h>, C11 §7.14.1.1), where v7 wrote
 * `register int (*istat)()' and let the empty parens mean anything at all.
 *
 * The status is the raw one wait() gives, not a shell exit code: v7 returns it as it
 * stands and every caller since has had to shift it.  It also rides in r12 on the way
 * out of the gate -- fifteen bits -- so a child whose exit code passes 127 is reported
 * truncated (lib/README.md, "Ground rules").  The 127 below, which is what the child
 * reports when the shell cannot be exec'd, is the largest code that survives intact.
 *
 * The child ends with _exit() and not exit(): there is nothing of ours to flush in it,
 * and flushing would write the parent's buffered output a second time.
 *
 * `system' is declared by <stdlib.h>; nothing else here is declared by any header.
 * The one thing C11 asks for that v7 did not have is the null-pointer case below.
 */
#include <signal.h>
#include <stdlib.h>

int fork(void);
int wait(int *status);
int execl(const char *name, ...);
int access(const char *path, int mode);
_Noreturn void _exit(int status);

int system(const char *s)
{
    int status, pid, w;
    void (*istat)(int);
    void (*qstat)(int);

    /*
     * §7.22.4.8: a null pointer asks only whether a command processor exists, and
     * nothing is run.  v7 had no such case -- there was no standard to have it for --
     * and the honest answer on this system is whether /bin/sh can be executed.
     */
    if (s == 0)
        return access("/bin/sh", 1) == 0;

    if ((pid = fork()) == 0) {
        execl("/bin/sh", "sh", "-c", s, (char *)0);
        _exit(127);
    }
    istat = signal(SIGINT, SIG_IGN);
    qstat = signal(SIGQUIT, SIG_IGN);
    while ((w = wait(&status)) != pid && w != -1)
        ;
    if (w == -1)
        status = -1;
    signal(SIGINT, istat);
    signal(SIGQUIT, qstat);
    return status;
}
