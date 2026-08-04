//
// unprivt -- ps(1) and df(1M) work for a user who is not root, and the devices still do not.
//
// IMAGE ONLY, like suidt.c next door and for its reason: the premise is a real proc table, a
// real mounted root and real uids, none of which b6sim has.
//
// Both programs used to be the super-user's -- ps read /dev/kmem and /dev/mem for four u-area
// fields, df read the superblock off /dev/rmd0.  They are ordinary commands now because the
// kernel answers instead (KCTL_PSINFO in <sys/kctl.h>, statfs(2) in <sys/statfs.h>), NOT
// because any mode was loosened.  Hence the negative control below, which is the load-bearing
// half: without it, "ps printed a table" would be equally consistent with somebody having
// quietly widened a device node, which is the outcome this whole change is defined against.
//
// Verdicts only in the .expected -- no pid, no uid, no block count.
//
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define NOBODY 7 // guest, /etc/passwd
#define NOGRP  3 // bin, /etc/group

#define PSOUT  "/tmp/unprivt.ps"
#define DFOUT  "/tmp/unprivt.df"
#define DFTMP  "/tmp/unprivt.dt"
#define DFERR  "/tmp/unprivt.de"
#define DFNULL "/tmp/unprivt.dn"

// Bits the negative-control child reports through its exit status.
#define NC_KMEM 1
#define NC_MEM  2
#define NC_RMD0 4

// File scope: 4096 bytes is 683 words, and the stack is 4096 WORDS (../../cmd/README.md SS6).
static char buf[4096];
static char line[256];

static void ok(const char *what, int cond)
{
    printf("%s %s\n", cond ? "ok  " : "FAIL", what);
}

// Become guest.  setgid() first: setuid() moves the real uid too, after which suser() says no.
static int becomeguest(void)
{
    if (setgid(NOGRP) < 0)
        return -1;
    return setuid(NOBODY) < 0 ? -1 : 0;
}

// Run one command as guest with stdout on `out' and stderr on `err', either of which may be
// null for "leave it alone".  close-then-creat is v7's redirection: the new descriptor is the
// lowest free one.  Returns the wait status, or -1.
static int rundropped(const char *path, const char *a1, const char *a2, const char *out,
                      const char *err)
{
    int pid, status;

    fflush(stdout);
    pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        if (becomeguest() < 0)
            _exit(126);
        if (out != NULL) {
            close(1);
            if (creat(out, 0666) != 1)
                _exit(125);
        }
        if (err != NULL) {
            close(2);
            if (creat(err, 0666) != 2)
                _exit(125);
        }
        if (a1 == NULL)
            execl(path, path, (char *)0);
        else if (a2 == NULL)
            execl(path, path, a1, (char *)0);
        else
            execl(path, path, a1, a2, (char *)0);
        _exit(127);
    }
    while (wait(&status) != pid)
        ;
    return status;
}

static int ranok(int status)
{
    return status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

// Slurp a file into buf, NUL-terminated.  Returns its length, or -1.
static int slurp(const char *path)
{
    int fd, n;

    if ((fd = open(path, O_RDONLY)) < 0)
        return -1;
    n = read(fd, buf, sizeof buf - 1);
    close(fd);
    if (n < 0)
        return -1;
    buf[n] = '\0';
    return n;
}

// The first line of buf containing `what', copied into line[].  Returns 0 if there is none.
static int linewith(const char *what)
{
    char *p, *q;
    int i;

    for (p = buf; *p != '\0';) {
        for (q = p; *q != '\0' && *q != '\n'; q++)
            ;
        for (i = 0; i < (int)sizeof line - 1 && p + i < q; i++)
            line[i] = p[i];
        line[i] = '\0';
        if (strstr(line, what) != NULL)
            return 1;
        p = (*q == '\0') ? q : q + 1;
    }
    line[0] = '\0';
    return 0;
}

// How many lines buf holds.
static int nlines(void)
{
    char *p;
    int n = 0;

    for (p = buf; *p != '\0'; p++)
        if (*p == '\n')
            n++;
    return n;
}

// Field `n' of line[], counting from 1, into `dst'.  0 if there is no such field.
static int field(int n, char *dst, int size)
{
    char *p = line;
    int i;

    while (n-- > 0) {
        while (*p == ' ')
            p++;
        if (*p == '\0')
            return 0;
        if (n > 0)
            while (*p != '\0' && *p != ' ')
                p++;
    }
    for (i = 0; i < size - 1 && p[i] != '\0' && p[i] != ' '; i++)
        dst[i] = p[i];
    dst[i] = '\0';
    return i > 0;
}

int main(void)
{
    struct stat st;
    int status, pid, bits;
    char total1[32], total2[32], name1[32], name2[32];

    ok("we start as root", getuid() == 0 && geteuid() == 0);
    ok("/bin/ps is not setuid",
       stat("/bin/ps", &st) == 0 && (st.st_mode & 07777) == 0755 && st.st_uid == 0);
    ok("/bin/df is not setuid",
       stat("/bin/df", &st) == 0 && (st.st_mode & 07777) == 0755 && st.st_uid == 0);

    // THE NEGATIVE CONTROL FIRST.  A child that execs nothing and keeps uid 7 must still be
    // refused all three nodes; if it is not, everything below proves nothing.
    fflush(stdout);
    pid = fork();
    if (pid == 0) {
        int b = 0, fd;
        if (becomeguest() < 0)
            _exit(0);
        if ((fd = open("/dev/kmem", O_RDONLY)) < 0 && errno == EACCES)
            b |= NC_KMEM;
        else if (fd >= 0)
            close(fd);
        if ((fd = open("/dev/mem", O_RDONLY)) < 0 && errno == EACCES)
            b |= NC_MEM;
        else if (fd >= 0)
            close(fd);
        if ((fd = open("/dev/rmd0", O_RDONLY)) < 0 && errno == EACCES)
            b |= NC_RMD0;
        else if (fd >= 0)
            close(fd);
        _exit(b);
    }
    while (wait(&status) != pid)
        ;
    bits = WIFEXITED(status) ? WEXITSTATUS(status) : 0;
    ok("guest still cannot open /dev/kmem", (bits & NC_KMEM) != 0);
    ok("guest still cannot open /dev/mem", (bits & NC_MEM) != 0);
    ok("guest still cannot open /dev/rmd0", (bits & NC_RMD0) != 0);

    // ps, as guest.  -a and -x so the table is everybody's, which is the case that used to
    // need /dev/mem for every row but the caller's.
    status = rundropped("/bin/ps", "-alx", NULL, PSOUT, NULL);
    ok("guest ran /bin/ps to a normal exit", ranok(status));
    ok("and it printed something", slurp(PSOUT) > 0);
    ok("with the long header", linewith("PID") && linewith("WCHAN") && linewith("CMD"));
    ok("and more than one process", nlines() >= 3);
    // The one row whose command name this program independently knows: its own parent.
    ok("including a row for unprivt itself", linewith("unprivt"));
    ok("that row is not <swapped>", strstr(line, "swapped") == NULL);
    ok("and carries a terminal, not a question mark", field(13, name1, sizeof name1) &&
                                                          strcmp(name1, "?") != 0);

    // df with no argument: the asked route, on the root.
    status = rundropped("/bin/df", NULL, NULL, DFOUT, NULL);
    ok("guest ran /bin/df to a normal exit", ranok(status));
    ok("and it printed something", slurp(DFOUT) > 0);
    ok("with the Berkeley header", linewith("1K-blocks") && linewith("Capacity"));
    ok("and a row for the root", linewith("/dev/md0"));
    ok("mounted on /", field(6, name1, sizeof name1) && strcmp(name1, "/") == 0);
    ok("with a nonzero size", field(1, name1, sizeof name1) && field(2, total1, sizeof total1) &&
                                  atoi(total1) > 0);

    // df on a PATH, not a device: the same filesystem, reached through st_dev.  Only the
    // Filesystem name and the volume size are compared -- Used and Avail move as this test
    // writes its own output files.
    status = rundropped("/bin/df", "/tmp", NULL, DFTMP, NULL);
    ok("guest ran /bin/df /tmp to a normal exit", ranok(status));
    ok("naming the same filesystem",
       slurp(DFTMP) > 0 && linewith("/dev/md0") && field(1, name2, sizeof name2) &&
           strcmp(name1, name2) == 0);
    ok("of the same size", field(2, total2, sizeof total2) && strcmp(total1, total2) == 0);

    // The read route, refused.  It must fail CLEANLY: the new diagnostic on stderr, no table
    // on stdout, and v7's exit status of 0 all the same.
    status = rundropped("/bin/df", "-w", "/dev/rmd0", DFNULL, DFERR);
    ok("guest ran /bin/df -w /dev/rmd0 and it still exited 0", ranok(status));
    ok("it printed no table", slurp(DFNULL) == 0);
    ok("and said the raw device is the super-user's",
       slurp(DFERR) > 0 && linewith("super-user"));
    ok("naming what to do instead", strstr(line, "mount point") != NULL);

    unlink(PSOUT);
    unlink(DFOUT);
    unlink(DFTMP);
    unlink(DFERR);
    unlink(DFNULL);
    return 0;
}
