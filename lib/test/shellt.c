//
// shellt -- system() and popen()/pclose() WITH A SHELL THAT REALLY STARTS.
//
// THIS PROGRAM RUNS ON THE DISK IMAGE ONLY, never under b6sim, and spawn.c next door is
// its other half.  Both routines fork a child that execs /bin/sh; under b6sim that exec
// always fails, because the host's /bin/sh is not a BESM-6 a.out, so spawn.c can only
// test the arm in which the shell is unreachable -- 127 out of system(), 1 out of popen(),
// an empty pipe.  Here the shell is on the image, the exec succeeds, and everything past
// it becomes reachable for the first time: the command's exit status coming back through
// wait(), the child's inherited descriptors, and a pipe with data in it.
//
// The deleted image-side runner ran it as ./shellt from /usr/test with both descriptors on
// a file and diffed the result; nothing runs it now -- lib/test/CMakeLists.txt
// registers it IMAGEONLY, so no b6sim case exists to fail.
//
// WHAT MAY REACH THE .expected FILE.  No pid, no descriptor number and no time: the only
// numbers printed are wait statuses, which are the program's own arithmetic.  The
// commands are spelled with ABSOLUTE PATHS so that nothing depends on what PATH the shell
// defaulted to, and `exit 3' is the shell's own builtin, so the one non-zero status here
// needs no program to produce it.
//
// 3 << 8 IS 768, well inside the fifteen bits a status has to cross in r12 (lib/README.md,
// "Ground rules").  spawn.c's 127 sits exactly on that line and says so; this one does not
// have to.
//
// fflush() BEFORE EVERY system() AND popen(): the child inherits descriptor 1 and writes
// to it directly, so anything still in this program's stdio buffer would be written after
// the child's output and the transcript would come out in the wrong order.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define TMP1 "/tmp/shellt.tmp"
#define TMP2 "/tmp/shellt2.tmp"

static void ok(const char *what, int cond)
{
    printf("%s %s\n", cond ? "ok  " : "FAIL", what);
}

//
// The first line of a file, with its newline stripped, into buf.  Returns 0 if the file
// could not be opened or held nothing -- which is a failure of whatever was supposed to
// have written it, and shows up as the verdict that reads the answer.
//
static int firstline(const char *path, char *buf, int n)
{
    FILE *f;
    char *p;

    if ((f = fopen(path, "r")) == NULL)
        return 0;
    p = fgets(buf, n, f);
    fclose(f);
    if (p == NULL)
        return 0;
    if ((p = strchr(buf, '\n')) != NULL)
        *p = '\0';
    return 1;
}

int main(void)
{
    FILE *f;
    char buf[64];
    int status;

    //
    // §7.22.4.8's null-pointer case, which spawn.c deliberately leaves alone because the
    // answer there is a question about the host.  Here it is a question about the image,
    // and the answer is fixed: /bin/sh is on it, mode 0755 (../../root.manifest).
    //
    printf("command processor %d\n", system((char *)0));

    //
    // system(): the child's descriptor 1 is this program's, so the shell's output lands in
    // the same stream as these verdicts and its position in the file is part of the check.
    //
    fflush(stdout);
    status = system("/bin/echo echoed by the shell");
    printf("system status %d\n", status);
    ok("system reaped its own child", status == 0);
    ok("system did not disturb wait", wait(0) == -1);

    // A command that redirects: what the shell wrote is read back here.
    fflush(stdout);
    status = system("/bin/echo written by the shell >" TMP1);
    ok("the redirected command succeeded", status == 0);
    buf[0] = '\0';
    ok("and the file holds what it echoed",
       firstline(TMP1, buf, sizeof buf) && strcmp(buf, "written by the shell") == 0);

    //
    // A non-zero exit, from the shell's own `exit' builtin: no program is involved, so
    // this tests the status path and nothing else.  wait() gives the raw status, code in
    // the high byte, as it has since v7.
    //
    fflush(stdout);
    status = system("exit 3");
    printf("exit status %d\n", status);
    ok("a non-zero exit comes back shifted", status == 3 * 256);

    //
    // popen("r"): the child writes down the pipe and this end reads it.  Under b6sim this
    // stream is at end of file at once, because the child died without writing.
    //
    fflush(stdout);
    f = popen("/bin/echo piped through the shell", "r");
    ok("popen r returns a stream", f != NULL);
    if (f != NULL) {
        buf[0] = '\0';
        ok("the pipe carried the line", fgets(buf, sizeof buf, f) != NULL);
        printf("popen r read \"%s", buf);
        printf("\"\n");
        ok("and nothing followed it", fgets(buf, sizeof buf, f) == NULL);
        status = pclose(f);
        printf("pclose status %d\n", status);
        ok("pclose reaped its own child", status == 0);
    }

    //
    // popen("w"): this end writes, cat at the far end puts it in a file.  The write is
    // safe here -- the child is alive, so there is no SIGPIPE to take this program down,
    // which is exactly why spawn.c closes its "w" stream unwritten.
    //
    fflush(stdout);
    f = popen("/bin/cat >" TMP2, "w");
    ok("popen w returns a stream", f != NULL);
    if (f != NULL) {
        fputs("down the write end\n", f);
        status = pclose(f);
        printf("pclose status %d\n", status);
        ok("pclose reaped the w child too", status == 0);
        buf[0] = '\0';
        ok("and cat wrote what it was given",
           firstline(TMP2, buf, sizeof buf) && strcmp(buf, "down the write end") == 0);
    }

    ok("no children are left", wait(0) == -1);
    unlink(TMP1);
    unlink(TMP2);
    printf("done\n");
    return 0;
}
