//
// coret -- the two places the kernel FABRICATES a user address out of an integer, driven
// from user mode.  kernel task 34, the `int'-vs-pointer sweep, is what this closes.
//
// THIS PROGRAM RUNS ON THE DISK IMAGE ONLY, never under b6sim, and neither half of it
// could run there: b6sim has no core dump at all, and it refuses ptrace(2) with EPERM
// (cmd/sim/syscall.cpp).  Both arms below are about what the KERNEL does with an address
// it did not receive as a pointer, so the host serving the call would answer the wrong
// question -- which is exactly how the two bugs the sweep started from survived review.
//
// THE RULE BEING TESTED.  `(caddr_t)<some int>' on this machine is a silent bit COPY: the
// bit-48 marker stays clear and the byte-offset field reads 0, which -- since 5 is a
// word's FIRST byte and 0 its last -- looks like BYTE #5.  The conversion that yields
// byte #0 of word w is the two-step `(caddr_t)(int *)w'.  doc/Besm6_Data_Representation.md
// section 7 is the layout; <sys/param.h>'s ptrword()/ptrbyte() are the accessors.
//
// ARM 1 -- THE CORE DUMP.  core() (kernel/sig.c) writes the u-area, then the user's whole
// image, through u_base.  That second base was the literal 0, which is not a fat pointer:
// no marker, and a byte field of 0 -- byte #5, a word's LAST.  So it stood out of phase
// with the kernel buffer and copyinb() funnelled the first 3072-byte chunk five bytes
// over.  ONLY THE FIRST: iomove() walks the base with `u.u_base += n' and the walked value
// is well-formed, so everything above virtual word 511 was right.  The damage was the head
// of the image -- the header hole and the start of const+text -- and nothing on this
// system had ever read a core file back, so nothing said so.
//
// WHICH IS WHY THERE ARE TWO CONTENT CHECKS, and why the second one is the load-bearing
// one.  A child fills an array with a function of the index and dies on SIGABRT; the
// parent computes the same function again and compares it against what reached the disk.
// That array is in bss, well past word 511, and would have passed against the broken
// kernel -- it says the bulk arm still works.  The check that FAILS without the fix reads
// the first chunk, comparing the core file's copy of the const segment against the running
// image at the same virtual addresses.  Neither check is a round trip: each has an oracle
// the copy under test did not produce.
//
// ARM 2 -- ptrace's ip_addr.  Nothing had ever called ptrace(2) on this port: no adb, no
// sdb, and the shell only reports the stop.  So the unit of the third argument was
// unwritten, and `(caddr_t)ipc.ip_addr' in procxmt() was right only by assumption.  It is
// a WORD address now, said in doc/Unix_V7_System_Calls.md section 4, and this is what
// holds it to that.  The poke is checked through the CHILD -- it resumes, reads the word
// with its own instructions and exits with it -- so the value never comes back out the way
// it went in.
//
// -1 IS A LEGAL ptrace RESULT, so every check that is about SUCCESS clears errno first and
// reads it afterwards; only the u-area bounds test can use the return value directly,
// because an out-of-range request is refused before any word is fetched.  lib/libc's stub
// carries no `errno = 0' preamble for exactly this reason -- see ptrace(2).
//
// WHAT MAY REACH THE CONSOLE: verdicts, and numbers this program already knew.  The .ini
// that runs it matches on the last line.
//
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>

// No header declares this one: ptrace has a libc stub (lib/libc/sys/syscalls.tbl) and no
// prototype anywhere, this being its first caller in the tree.
int ptrace(int req, int pid, int *addr, int data);

// The pattern.  Every word differs from its neighbours, so a copy displaced by five bytes
// -- which joins the tail of one word to the head of the next -- matches nowhere.  Both
// terms stay well inside the 41 bits an int has.
#define NPAT   64
#define PAT(i) (0525000000000 + (i) * 01020304)

// What the parent pokes into the child, and what the child must then exit with.  One byte:
// wait()'s status rides in r12, an index register, so an exit code above 127 arrives
// truncated (<sys/wait.h> says so at length).
#define POKED 0125

// File scope, not locals: `struct user' is a whole 1024-word page and the stack is four
// pages.  cmd/README.md section 6 is the account of that ceiling.
static struct user ku; // this process's u-area, read back through /dev/kmem
static struct proc pr; // ...and the proc entry it points at

// The array the core dump is checked over.  Written once before the fork and read only by
// the kernel's copy after that.
static int pattern[NPAT];

// The word arm 2 pokes.  Volatile so the child really loads it after resuming, rather
// than carrying a copy in a register across the stop.
static volatile int datum = 0;

static void ok(const char *what, int cond)
{
    printf("%s %s\n", cond ? "ok  " : "FAIL", what);
}

// This process's p_size, in words, through /dev/kmem -- the same route memt takes and for
// the same reason: the u-area is at a fixed physical address, so a header constant is all
// it takes to find it.  Returns 0 if anything on the way fails.
//
// IT PRINTS NOTHING, and its caller must not either between this and the fork: stdio's
// arena grows the image, and the number wanted is the one the child will inherit.
static int mysize(void)
{
    int km, n;

    km = open("/dev/kmem", 0);
    if (km < 0)
        return 0;
    n = 0;
    if (lseek(km, (off_t)UBASE * NBPW, 0) >= 0 && read(km, &ku, sizeof ku) == (int)sizeof ku &&
        lseek(km, (off_t)(int)ku.u_procp * NBPW, 0) >= 0 &&
        read(km, &pr, sizeof pr) == (int)sizeof pr && pr.p_pid == getpid())
        n = pr.p_size;
    close(km);
    return n;
}

// ---------------------------------------------------------------------------------------
// Arm 1: the core dump.
// ---------------------------------------------------------------------------------------
static void corearm(void)
{
    int i, pid, status, fd, psize, bad;
    int got[NPAT];
    off_t at, size;

    for (i = 0; i < NPAT; i++)
        pattern[i] = PAT(i);
    unlink("core");

    // The size and the fork, back to back and with nothing printed in between: the child's
    // image is this one, so its p_size is the one taken here.
    psize = mysize();
    pid   = fork();
    if (pid == 0) {
        // The child.  SIGABRT is one of the nine core() dumps for (kernel/sig.c), and the
        // DEFAULT disposition is what this wants: no handler means no sendsig(), so the
        // image is not grown between the fork and the dump.
        kill(getpid(), SIGABRT);
        _exit(1); // not reached; the signal is fatal
    }

    ok("this process can read its own proc entry", psize > 0);
    ok("the child forked", pid > 0);
    status = 0;
    ok("and it died on SIGABRT", wait(&status) == pid && WTERMSIG(status) == SIGABRT);
    ok("dumping core", WCOREDUMP(status) != 0);

    fd = open("core", 0);
    ok("the core file is there", fd >= 0);
    if (fd < 0)
        return;

    // THE SIZE.  core() writes USIZE words of u-area and then p_size - USIZE words of
    // image, so the whole file is p_size words.  This one has never failed; it is here so
    // that a truncated dump is told apart from a shifted one rather than diagnosed as it.
    size = lseek(fd, (off_t)0, 2);
    printf("core %d bytes, image %d words\n", (int)size, psize);
    ok("it is p_size words long -- the u-area page, then the image", size == (off_t)wtob(psize));

    // THE CONTENTS.  Virtual word V of the image sits at byte wtob(USIZE + V): the u-area
    // page comes first, and the image is dumped from virtual 0.  `pattern' is at word
    // (int)pattern -- an int * is a bare word address on this machine, so the cast is the
    // whole conversion.
    at  = (off_t)wtob(USIZE + (int)pattern);
    bad = !(lseek(fd, at, 0) == at && read(fd, got, sizeof got) == (int)sizeof got);
    ok("the array reads back out of it", !bad);
    if (!bad) {
        for (i = 0; i < NPAT; i++)
            if (got[i] != PAT(i))
                break;
        if (i != NPAT)
            printf("word %d is %o, wanted %o\n", i, got[i], PAT(i));
        ok("and every word of it is where it belongs", i == NPAT);
    }

    // THE FIRST BLOCK, AND IT IS THE ONE THAT MATTERS.  iomove() walks the base with
    // `u.u_base += n' and the walked value is well formed, so a fabricated base is out of
    // phase for the FIRST 3072-byte chunk only and right for the thirteen after it -- the
    // array above lives past that chunk and cannot see the bug at all.  Measured on the
    // broken kernel with ucopy.c's nioshift, which rose by exactly one block.
    //
    // The oracle is the running image itself, at the same virtual addresses: word 8 up is
    // the const segment (BADDR, an impure image), which fork copied and neither process
    // writes.  Words 0..7 are the header hole and word 0 is the black hole, so the compare
    // starts above them.
    at  = (off_t)wtob(USIZE + BADDR);
    bad = !(lseek(fd, at, 0) == at && read(fd, got, sizeof got) == (int)sizeof got);
    ok("the first block of the image reads back too", !bad);
    if (!bad) {
        for (i = 0; i < NPAT; i++)
            if (got[i] != ((int *)BADDR)[i])
                break;
        if (i != NPAT)
            printf("word %d of the const segment is %o, wanted %o\n", i, got[i], ((int *)BADDR)[i]);
        ok("and it is in phase -- the first chunk is not shifted", i == NPAT);
    }
    close(fd);
    unlink("core");
}

// ---------------------------------------------------------------------------------------
// Arm 2: ptrace's ip_addr.
// ---------------------------------------------------------------------------------------
static void ptracearm(void)
{
    int pid, status;

    datum = 0;
    pid   = fork();
    if (pid == 0) {
        // The child.  ptrace(0) marks itself traced; the signal then stops it rather than
        // killing it, and the parent is told through wait().  After it is continued it
        // reads `datum' with its own instructions -- which is what makes the poke below an
        // assertion rather than a round trip -- and hands it back as its exit code.
        ptrace(0, 0, (int *)0, 0);
        kill(getpid(), SIGTRAP);
        _exit(datum & 0377);
    }
    status = 0;
    ok("the traced child stopped rather than died",
       wait(&status) == pid && WIFSTOPPED(status) && WSTOPSIG(status) == SIGTRAP);

    // REQUEST 3 -- read u.  ip_addr is a WORD INDEX into the u-area, bounded by USIZE;
    // v7's was a byte offset bounded by 512.  The two ends and one past the top say which:
    // if the unit were bytes, USIZE-1 would still be inside the page and USIZE would too.
    errno = 0;
    ptrace(3, pid, (int *)0, 0);
    ok("read-u takes word index 0", errno == 0);
    errno = 0;
    ptrace(3, pid, (int *)(USIZE - 1), 0);
    ok("...and the last word of the u-area page", errno == 0);
    ok("...and refuses one past it", ptrace(3, pid, (int *)USIZE, 0) == -1);

    // REQUESTS 5 AND 2 -- write D, read D.  `&datum' is an int *, i.e. a bare word address,
    // and that is the interface: there is no byte to name.  The child's copy of the word is
    // at the same address as ours, fork having copied the image.
    errno = 0;
    ptrace(5, pid, (int *)&datum, POKED);
    ok("write-D reports no error", errno == 0);
    ok("read-D brings the poked word back", ptrace(2, pid, (int *)&datum, 0) == POKED);

    // ...and the child agrees, having loaded it itself.  A poke that landed on the wrong
    // word would still read back through request 2 -- it is the same address either way --
    // so this is the leg that says the address meant what the ABI says it means.
    ptrace(7, pid, (int *)1, 0); // continue where it stopped, no signal
    status = 0;
    ok("the child resumed and read the poked word for itself",
       wait(&status) == pid && WIFEXITED(status) && WEXITSTATUS(status) == POKED);
    ok("this process's own copy is untouched", datum == 0);
}

int main(void)
{
    printf("--- core(2) writes the image at byte #0 of word 0\n");
    if (chdir("/tmp") < 0) {
        ok("chdir /tmp", 0);
        printf("coret done\n");
        return 1;
    }
    corearm();

    printf("--- ptrace's addr is a word address\n");
    ptracearm();

    printf("coret done\n");
    return 0;
}
