/*
 * procs -- the syscalls with a second result, and the ones whose gate takes no
 * arguments at all.
 *
 * Five stubs in sys/ exist only because the gate answers in r12: getpid/getppid,
 * getuid/geteuid, getgid/getegid, fork and wait -- plus pipe, which returns BOTH
 * descriptors that way and fills the caller's array itself.  Nothing in C can see r12,
 * so if any of those stubs read the wrong register, or let the extracode overwrite the
 * pointer it was handed, this is where it shows.
 *
 * The gate arity is under test too, and silently: fork, wait, pipe and getpid take no
 * arguments, so the gate must not pop the user stack for them.  A stub that got that
 * wrong drifts r15 by a word per call, and after enough calls main() returns into
 * rubbish -- which is why the calls below are made in a loop's worth of succession and
 * the program still has to reach its last line.
 *
 * Exit statuses stay small deliberately: wait's status rides in r12, an index register,
 * and (code << 8) passes 15 bits as soon as the code passes 127.  See lib/README.md.
 *
 * Like hello.c it declares its syscalls itself and carries its own output routines.
 */

int write(int fd, char *buf, int n);
int read(int fd, char *buf, int n);
int close(int fd);
int dup(int fd);
int dup2(int fd, int fd2);
int time(int *tloc);
int getpid(void);
int getppid(void);
int getuid(void);
int geteuid(void);
int getgid(void);
int getegid(void);
int fork(void);
int wait(int *status);
int pipe(int *fildes);
void _exit(int status);

/* One string to the standard output, without stdio (phase 4). */
static void put(char *s)
{
    char *p = s;
    int n   = 0;

    while (*p) {
        p++;
        n++;
    }
    write(1, s, n);
}

/* One decimal digit, taken out of a literal: there is no itoa yet (phase 2). */
static void putdigit(int d)
{
    char *p = "0123456789";

    while (d-- > 0)
        p++;
    write(1, p, 1);
}

static void putnum(int v)
{
    if (v < 0) {
        put("-");
        v = -v;
    }
    if (v >= 10)
        putnum(v / 10);
    putdigit(v % 10);
}

/* Report a claim by name, so the .expected file reads as a checklist. */
static void ok(char *what, int cond)
{
    put(cond ? "ok   " : "FAIL ");
    put(what);
    put("\n");
}

int main(int argc, char **argv, char **envp)
{
    int fd[2], status, pid, child, dfd, now;
    char buf[12];

    /*
     * getpid and getppid are one extracode with two entry points; the second reads r12.
     * Both must be real pids, and they must differ -- a stub that ignored r12 would
     * return the same number twice and pass a weaker test than this one.
     */
    pid = getpid();
    ok("getpid is positive", pid > 0);
    ok("getppid is positive", getppid() > 0);
    ok("getpid and getppid differ", getpid() != getppid());
    ok("getpid is stable", getpid() == pid);

    /* The id pairs are the same shape: real in the accumulator, effective in r12. */
    ok("getuid is not negative", getuid() >= 0);
    ok("geteuid is not negative", geteuid() >= 0);
    ok("getgid is not negative", getgid() >= 0);
    ok("getegid is not negative", getegid() >= 0);

    /*
     * pipe fills the array from two registers.  Writing one end and reading the other
     * proves the two descriptors were not stored the same way round or into the same
     * slot -- either mistake still yields two plausible small numbers.
     */
    ok("pipe succeeds", pipe(fd) == 0);
    ok("pipe ends differ", fd[0] != fd[1]);
    ok("pipe write", write(fd[1], "through", 7) == 7);
    ok("pipe read", read(fd[0], buf, 7) == 7);
    buf[7] = 0;
    put("pipe carried \"");
    put(buf);
    put("\"\n");

    /*
     * dup and dup2 share a two-argument gate whichever name is called, so the stub
     * pushes a word the C caller did not.  Get that wrong and the gate reads the
     * descriptor from an untouched word AND pops the caller's stack: the calls below
     * would return plausible small numbers while everything after them drifted.  The
     * duplicate really has to be the same pipe, hence the write-through.
     */
    dfd = dup(fd[1]);
    ok("dup returns a new descriptor", dfd >= 0 && dfd != fd[1]);
    ok("the duplicate is the same pipe", write(dfd, "dup", 3) == 3);
    ok("dup round-trip", read(fd[0], buf, 3) == 3);
    close(dfd);

    dfd = dup2(fd[1], dfd);
    ok("dup2 lands on the descriptor asked for", dfd >= 0);
    ok("dup2 gave the same pipe", write(dfd, "two", 3) == 3);
    ok("dup2 round-trip", read(fd[0], buf, 3) == 3);
    close(dfd);

    ok("dup of a closed descriptor fails", dup(dfd) == -1);

    close(fd[0]);
    close(fd[1]);

    /*
     * time() also has a gate that takes no arguments: the seconds come back in the
     * accumulator and the store through the caller's pointer is the stub's own doing.
     * The pointer arrives in the accumulator too, so a stub that failed to park it
     * before the trap would write the time through the time.
     */
    now = 0;
    ok("time returns a plausible epoch", time(0) > 0);
    ok("time stores through its pointer", time(&now) == now && now > 0);

    /*
     * fork: the child gets 0 and the parent the child's pid.  The gate hands each side
     * the OTHER's pid and says which side you are in r12, so the 0 the child sees below
     * is manufactured by sys/fork.s and is the thing being tested.
     */
    child = fork();
    if (child == 0) {
        _exit(7);
    }
    ok("fork returns a child pid to the parent", child > 0);

    /* wait: the pid in the accumulator, the status through the caller's pointer. */
    status = -1;
    pid    = wait(&status);
    ok("wait returns the child", pid == child);
    put("child status ");
    putnum(status);
    put("\n");
    ok("child exited 7", status == 7 * 256);

    /* wait(0) is legal and must not store through a null pointer. */
    ok("wait with no children fails", wait(0) == -1);

    put("done\n");
    return 0;
}
