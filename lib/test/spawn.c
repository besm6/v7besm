/*
 * spawn -- system() and popen()/pclose(), the two routines that start a shell.
 *
 * NO SHELL IS EVER STARTED HERE, and that is what makes the test deterministic.
 * /bin/sh on the host is not a BESM-6 a.out, so the child's execl() always fails and
 * the child reports it by exiting -- 127 from system(), 1 from popen(), which are the
 * two codes those routines chose for exactly this case.  Everything else in them runs
 * for real: fork, pipe, dup2, the pair of signal(SIG_IGN) calls and their restoration,
 * the wait loop that skips over children it did not start, and, on the popen side,
 * fdopen() and fclose() over a live pipe descriptor.
 *
 * 127 << 8 IS 32512, which is the largest wait status that survives the trip: the
 * status comes back in r12, an index register of fifteen bits, so an exit code above
 * 127 would arrive truncated (lib/README.md, "Ground rules").  system()'s own choice of
 * 127 sits exactly on that line, which is worth knowing and is why the number is
 * printed rather than merely compared.
 *
 * The "w" stream is closed WITHOUT WRITING TO IT.  Its child is already gone, so a
 * write would draw SIGPIPE, whose default action would take this program down with it
 * -- and signal delivery is phase 6, so there would be nothing to catch it with.
 *
 * system(NULL) is deliberately not exercised: §7.22.4.8 makes it a question about the
 * host ("is there a command processor?"), and an .expected file may record only what
 * the program itself does.
 */
#include <stdio.h>
#include <stdlib.h>

int wait(int *status);

static void ok(const char *what, int cond)
{
    printf("%s %s\n", cond ? "ok  " : "FAIL", what);
}

int main(void)
{
    FILE *f;
    char buf[64];
    int status;

    /*
     * system(): fork, exec the shell -- which fails -- and reap.  The exit code the
     * child chose for a shell it could not exec is 127.
     */
    fflush(stdout);
    status = system("echo nothing runs here");
    printf("system status %d\n", status);
    ok("system reaped its own child", status == 127 * 256);
    ok("system did not disturb wait", wait(0) == -1);

    /*
     * popen("r"): the pipe is built, the child takes the write end and dies without
     * writing, so the read end is at end of file at once.  A getc that answered
     * anything but EOF would mean the descriptors were crossed.
     */
    fflush(stdout);
    f = popen("echo nothing runs here", "r");
    ok("popen r returns a stream", f != NULL);
    if (f != NULL) {
        ok("the read end is at end of file", fgets(buf, sizeof buf, f) == NULL);
        ok("and says so", feof(f) != 0);
        status = pclose(f);
        printf("pclose status %d\n", status);
        ok("pclose reaped its own child", status == 1 * 256);
    }

    /* popen("w"): closed unwritten, since its child is gone and SIGPIPE would end us. */
    fflush(stdout);
    f = popen("echo nothing runs here", "w");
    ok("popen w returns a stream", f != NULL);
    if (f != NULL) {
        status = pclose(f);
        printf("pclose status %d\n", status);
        ok("pclose reaped the w child too", status == 1 * 256);
    }

    ok("no children are left", wait(0) == -1);
    printf("done\n");
    return 0;
}
