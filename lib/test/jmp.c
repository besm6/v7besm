//
// jmp -- setjmp and longjmp.
//
// These two are the only part of phase 2 that could not be ported at all: v7's is x86
// assembly, so lib/libc/gen/setjmp.s is written from the calling convention, and this
// program is the only thing that says whether it was read correctly.
//
// What each check is really asking:
//
//   THE RETURN VALUE.  setjmp must return 0 the first time and the longjmp's val the
//   second, and longjmp(env, 0) must be turned into 1 -- otherwise a caller cannot
//   tell the two arrivals apart.  The value travels in the accumulator the whole way
//   through longjmp; there is no cell to park it in, so a restore step that touched
//   the accumulator would show up here as a wrong or truncated value.  Hence a val
//   too big for an index register is tried as well.
//
//   THE STACK.  longjmp restores r15 from the buffer, discarding every frame between
//   the two points.  A drift of one word per jump would go unnoticed in a program that
//   returned straight away, so this one jumps out of a deep recursion many times over
//   and then goes on making calls and returning from main normally -- the same shape
//   the procs test uses against the syscall gate's stack cleanup.
//
//   THE REGISTERS.  r6 and r7 are the parameter and auto pointers of the function that
//   called setjmp; if they came back wrong, its locals would read as rubbish after the
//   jump.  So the locals are checked after each arrival, and one of them is written to
//   between the setjmp and the longjmp -- v7 says only `volatile' locals survive, but
//   nothing here relies on a register variable.
//
// There is no test of longjmp out of a signal handler: delivery is phase 6.
//
#include <setjmp.h>
#include <string.h>

int write(int fd, char *buf, int n);

static jmp_buf jb;
static int depth;

// One string to the standard output, without stdio (phase 4).
static void put(char *s)
{
    write(1, s, strlen(s));
}

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

static void ok(char *what, int cond)
{
    put(cond ? "ok   " : "FAIL ");
    put(what);
    put("\n");
}

// Recurse n frames deep, then jump out of the lot of them.
static void deep(int n, int val)
{
    int mine = n * 3 + 1; // an auto, so the frame is real

    depth++;
    if (n > 0) {
        deep(n - 1, val);
        return;
    }
    if (mine != 1)
        put("FAIL the recursion lost its own local\n");
    longjmp(jb, val);
}

// A function that returns normally, to prove the stack still works afterwards.
static int sum(int n)
{
    if (n <= 0)
        return 0;
    return n + sum(n - 1);
}

int main(int argc, char **argv, char **envp)
{
    int r, i, marker, seen;
    char local[8];

    // ---- the plain round trip ----
    marker = 12345;
    strcpy(local, "intact");
    r = setjmp(jb);
    if (r == 0) {
        marker = 54321;
        longjmp(jb, 7);
        put("FAIL longjmp returned to its caller\n");
    }
    ok("setjmp returns the longjmp's val", r == 7);
    ok("... and the locals are still readable", marker == 54321);
    ok("... including the array", strcmp(local, "intact") == 0);

    // ---- longjmp(env, 0) becomes 1 ----
    r = setjmp(jb);
    if (r == 0)
        longjmp(jb, 0);
    ok("longjmp(env, 0) arrives as 1", r == 1);

    // ---- a val too wide for an index register ----
    r = setjmp(jb);
    if (r == 0)
        longjmp(jb, 1000000);
    ok("val is a whole int, not 15 bits", r == 1000000);

    r = setjmp(jb);
    if (r == 0)
        longjmp(jb, -3);
    ok("a negative val survives", r == -3);

    // ---- out of a deep recursion, many times over ----
    seen = 0;
    for (i = 1; i <= 20; i++) {
        depth = 0;
        r     = setjmp(jb);
        if (r == 0) {
            deep(i, i + 100);
            put("FAIL deep() returned\n");
        }
        if (r != i + 100 || depth != i + 1)
            put("FAIL a jump out of the recursion went wrong\n");
        seen += r;
    }
    ok("twenty jumps out of a growing recursion", seen == 20 * 100 + 210);

    //
    // If r15 drifted by so much as a word per jump, the stack is now 20 words out and
    // the calls below are running over the frames of a main() that will still have to
    // return.  Recursing 60 deep and coming back is what would notice.
    //
    ok("the stack still works after all of them", sum(60) == 1830);
    ok("... and main's own locals are intact", marker == 54321 && r == 120);

    put("done\n");
    return 0;
}
