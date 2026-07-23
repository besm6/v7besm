//
// errno -- a failing syscall, seen from C.
//
// The stub is a bare `$77 N' and the gate reports failure in r14, a register the
// compiler destroys before any C statement can run.  So what is really under test is
// sys/cerror.s: that the failing arm is reached at all (`14 v1m cerror'), that r14 is
// banked into `errno' before anything else touches it, that the -1 handed back is the
// 41-bit int both gates return and not b6as's 48-bit all-ones, and that cerror returns
// to THIS function -- it is branched to, not called, so it returns on the stub's r13.
//
// It is also the first program to include a header from include/, which is the check
// that the v7 tree wins over the compiler's own (cmd/cc appends share/besm6/include
// after the user's -I).  <errno.h> is where `extern int errno' comes from.
//
// Like hello.c it declares its syscalls itself -- v7 has no <unistd.h> -- and carries
// its own output routines: stdio is phase 4 and the string routines phase 2, and taking
// either from the external c-compiler library would be the silent substitution
// lib/README.md warns about.
//
#include <errno.h>

int write(int fd, char *buf, int n);
int open(char *path, int mode);
int close(int fd);

// One string to the standard output, without stdio (phase 4).
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

// One decimal digit, taken out of a literal: there is no itoa yet (phase 2).
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

// "<what> = <ret> errno <errno>" -- the whole state a failed call leaves behind.
static void report(char *what, int ret)
{
    put(what);
    put(" = ");
    putnum(ret);
    put(" errno ");
    putnum(errno);
    put("\n");
}

int main(int argc, char **argv, char **envp)
{
    int r;

    //
    // A path that cannot exist.  -1 must come back as a value C compares equal to -1:
    // the accumulator carries the 41-bit form, and a stub that loaded b6as's `-1'
    // instead would set bits 48-42 and fail this comparison while still printing "-1".
    //
    errno = 0;
    r     = open("/no/such/file", 0);
    report("open", r);
    if (r != -1)
        put("BAD: open of a missing file succeeded\n");
    if (errno != ENOENT)
        put("BAD: errno is not ENOENT\n");

    // A descriptor that was never opened.  A second failure must replace the first.
    r = close(63);
    report("close", r);
    if (errno != EBADF)
        put("BAD: errno is not EBADF\n");

    //
    // A call that succeeds leaves errno ALONE -- it does not clear it.  The gate does
    // put 0 in r14, but nothing branches to cerror, so the word keeps its old value.
    // That is the v7 contract every caller relies on: test the result, then errno.
    //
    r = write(1, "write\n", 6);
    if (r != 6)
        put("BAD: short write\n");
    put("errno after a good call ");
    putnum(errno);
    put("\n");

    return 0;
}
