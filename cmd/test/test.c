/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// test -- evaluate a conditional expression and report through the exit status.
//
//      test expression
//      [ expression ]
//
// The one of task C2b's five that matters (../README.md): the v7 shell has no built-in for
// it, so until this program is on the image no script on this machine can branch.  It
// prints nothing when it succeeds -- 0 means true, 1 means false, and 255 means the
// expression did not parse -- which makes it the program in this tree best suited to
// b6_progtest(), whose harness asserts the exit status before it looks at the output at all.
//
// TWO NAMES, ONE INODE.  v7 links the binary a second time as `[', and the first thing
// main() does is look at argv[0] to find out which name it was called by, so that the
// closing `]' can be required and discarded.  root.manifest carries that as a `link' stanza
// -- the first hard link on this image; see README.md beside this file.
//
// THE C11 PASS IS THE WHOLE PORT, and it is a big one for 191 lines: nine K&R functions, all
// mutually recursive, all used before they are defined, one of them (`nxtarg') with an EMPTY
// declaration list so that its parameter was an implicit int.  Three things in it are worth
// naming:
//
//   exp() HAD TO BE RENAMED.  <math.h> declares `double exp(double)' and this libc really
//   provides it (../../lib/libm), so v7's top-level parse function collided with a standard
//   name.  Nothing here includes <math.h>, so it would have linked quietly and the collision
//   would have waited for whatever wanted the real one -- exactly the failure ../README.md §1
//   describes.  It is e0() now, which puts it in the e1/e2/e3 precedence family it heads.
//   DIR and FIL became T_DIR and T_FIL on the same principle.
//
//   synbad() IS _Noreturn, which is not decoration: it is what lets e3() and nxtarg() end
//   without a return statement, as they did in v7, rather than having a dead one added.
//
//   Everything at file scope is static.  ap/ac/av/tmp were four unqualified globals.
//
// There is no long, no %D and no buffer of any kind.
//
// THE 255 CANNOT BE SEEN, and that is worth knowing before someone changes it.  synbad()
// exits 255 as v7's did, but a wait status here is (code << 8) returned through r12, a
// fifteen-bit index register (lib/libc/sys/wait.S), so every code from 128 up arrives
// truncated and the shell's $? reads 127.  It is left at 255 anyway: the truncation is the
// system's ABI and affects every program that exits above 127, so moving this one number
// would hide a general limitation behind a local divergence -- and exit(2) really does
// receive 255, which is what b6sim's cases next door observe.  test.1 says so, and
// README.md beside this file is the account.
//
// -r AND -w STAY AN open(2) PROBE.  v7 implements them by opening the file and closing it
// again rather than by calling access(2), which reports on the REAL uid.  access(2) exists
// in this libc, but switching would be a divergence with nothing to show for it: every
// shell on this machine is root's (init execs /bin/sh with no getty and no login behind it),
// so the real and effective uids are the same 0 and both answers agree.  Left as v7 wrote
// it, and test.1 says which call it is.
//
// NOT SETUID: open, stat and isatty on the caller's own behalf need no privilege -- and a
// setuid test would answer -r and -w for the wrong user, which is the whole point of them.
//
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define EQ(a, b) ((tmp = a) == 0 ? 0 : (strcmp(tmp, b) == 0))

#define T_DIR 1
#define T_FIL 2

static int ap;    // the argument being looked at
static int ac;    // one past the last argument
static char **av; // argv
static char *tmp; // EQ()'s scratch, so that a null argument compares false

static _Noreturn void synbad(const char *s1, const char *s2);
static char *nxtarg(int mt);
static int e0(void);
static int e1(void);
static int e2(void);
static int e3(void);
static int tio(const char *a, int f);
static int ftype(const char *f);
static int fsizep(const char *f);
static int length(const char *s);

int main(int argc, char *argv[])
{
    ac = argc;
    av = argv;
    ap = 1;
    if (EQ(argv[0], "[")) {
        if (!EQ(argv[--ac], "]"))
            synbad("] missing", "");
    }
    argv[ac] = 0;
    if (ac <= 1)
        return 1;
    return e0() ? 0 : 1;
}

//
// The next argument.  With mt set, running off the end is an answer (a null pointer) rather
// than an error -- which is how the optional binary operators tell "nothing follows" from
// "something unexpected follows".
//
static char *nxtarg(int mt)
{
    if (ap >= ac) {
        if (mt) {
            ap++;
            return 0;
        }
        synbad("argument expected", "");
    }
    return av[ap++];
}

// expression -o expression
static int e0(void)
{
    int p1;

    p1 = e1();
    if (EQ(nxtarg(1), "-o"))
        return p1 | e0();
    ap--;
    return p1;
}

// expression -a expression
static int e1(void)
{
    int p1;

    p1 = e2();
    if (EQ(nxtarg(1), "-a"))
        return p1 & e1();
    ap--;
    return p1;
}

// ! expression
static int e2(void)
{
    if (EQ(nxtarg(0), "!"))
        return !e3();
    ap--;
    return e3();
}

// The primaries, and the parenthesised sub-expression.
static int e3(void)
{
    int p1;
    char *a;
    char *p2;
    int int1, int2;

    a = nxtarg(0);
    if (EQ(a, "(")) {
        p1 = e0();
        if (!EQ(nxtarg(0), ")"))
            synbad(") expected", "");
        return p1;
    }

    if (EQ(a, "-r"))
        return tio(nxtarg(0), 0);

    if (EQ(a, "-w"))
        return tio(nxtarg(0), 1);

    if (EQ(a, "-d"))
        return ftype(nxtarg(0)) == T_DIR;

    if (EQ(a, "-f"))
        return ftype(nxtarg(0)) == T_FIL;

    if (EQ(a, "-s"))
        return fsizep(nxtarg(0));

    if (EQ(a, "-t")) {
        if (ap >= ac)
            return isatty(1);
        return isatty(atoi(nxtarg(0)));
    }

    if (EQ(a, "-n"))
        return !EQ(nxtarg(0), "");
    if (EQ(a, "-z"))
        return EQ(nxtarg(0), "");

    p2 = nxtarg(1);
    if (p2 == 0)
        return !EQ(a, "");
    if (EQ(p2, "="))
        return EQ(nxtarg(0), a);

    if (EQ(p2, "!="))
        return !EQ(nxtarg(0), a);

    if (EQ(a, "-l")) {
        int1 = length(p2);
        p2   = nxtarg(0);
    } else {
        int1 = atoi(a);
    }
    int2 = atoi(nxtarg(0));
    if (EQ(p2, "-eq"))
        return int1 == int2;
    if (EQ(p2, "-ne"))
        return int1 != int2;
    if (EQ(p2, "-gt"))
        return int1 > int2;
    if (EQ(p2, "-lt"))
        return int1 < int2;
    if (EQ(p2, "-ge"))
        return int1 >= int2;
    if (EQ(p2, "-le"))
        return int1 <= int2;

    synbad("unknown operator ", p2);
}

//
// -r and -w: can the file be opened for reading (f == 0) or writing (f == 1)?  See the
// header on why this is not access(2).
//
static int tio(const char *a, int f)
{
    f = open(a, f);
    if (f >= 0) {
        close(f);
        return 1;
    }
    return 0;
}

static int ftype(const char *f)
{
    struct stat statb;

    if (stat(f, &statb) < 0)
        return 0;
    if ((statb.st_mode & S_IFMT) == S_IFDIR)
        return T_DIR;
    return T_FIL;
}

static int fsizep(const char *f)
{
    struct stat statb;

    if (stat(f, &statb) < 0)
        return 0;
    return statb.st_size > 0;
}

//
// Diagnostics go out through write(2) rather than stdio, as v7 wrote them: this program's
// whole answer is its exit status, and there is no output stream to keep in step.
//
static _Noreturn void synbad(const char *s1, const char *s2)
{
    write(2, "test: ", 6);
    write(2, s1, strlen(s1));
    write(2, s2, strlen(s2));
    write(2, "\n", 1);
    exit(255);
}

// -l: the length of a string.  The pointer difference is safe here; see the header.
static int length(const char *s)
{
    const char *es = s;

    while (*es++)
        ;
    return es - s - 1;
}
