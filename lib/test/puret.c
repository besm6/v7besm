//
// puret -- a PURE (NMAGIC) image: what the linker lays out and what exec() must load.
//
// The only program in the tree linked `b6ld -n', and the reason it exists is that until
// task 26 nothing was.  getxfile() (kernel/sys1.c) forces ux_tsize to 0 for an FMAGIC image,
// so xalloc() returns on its first line and the whole of kernel/text.c -- the shared text
// segment, its core, its copy on the paging store -- had never executed.  Everything that
// path does differently is exercised here:
//
//   * const and text form one READ-ONLY region starting at word BADDR (8), which the kernel
//     loads once and shares between every process running the binary;
//   * data is pushed to the next PAGE BOUNDARY (b6ld's -n aligns dorigin to 1024), and the
//     kernel maps it at virtual `ts' rather than folding it in with the text;
//   * so the data segment is read by a SECOND readi() with its own base, and it is that
//     second read where the pure path can differ from the impure one.
//
// "Pure" here means SHARED, not protected.  РЗ closes a page to reads as well as writes, so
// a read-only text page would take the program's own constant pool with it; estabur()
// accepts the xrw argument and ignores it (kernel/utab.c).  Nothing below tests for a fault
// on writing to the text, because there would not be one.
//
// IT RUNS IN BOTH WORLDS, and that is the point of the arrangement: under b6sim the loader
// is cmd/sim/machine.cpp and every system call is the host's, while off /usr/test it is
// getxfile() and the real thing.  One .expected file holds both to the same answer, so a
// disagreement is a bug in one of them rather than a fact about either (kernel/README.md).
//
// SECOND JOB: kernel/test/swap runs three copies of this program AT ONCE.  Each is given a
// digit as argv[1], writes it into its own data segment, and prints it back -- so a kernel
// that shared the data along with the text, or swapped one process's image over another's,
// shows up as a copy reporting somebody else's number.  With no argument (which is how
// b6sim and libtest run it) that stage reports the built-in default instead, so the
// expectation file is the same either way.
//
// Like hello.c it declares what it calls and carries its own output routines: stdio has its
// own data segment and would blur what this is measuring.
//

#include <unistd.h>

// The linker's segment boundaries.  b6ld defines all five (cmd/ld/ld.c); the two used here
// are the ends of text and data.  Under -n the text is PADDED to a page, so `etext' IS the
// data origin -- which is the one number this test is really about.
extern int etext[];
extern int edata[];
extern int end[];

// One string to the standard output, without stdio.
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

static void ok(char *what, int cond)
{
    put(cond ? "ok   " : "FAIL ");
    put(what);
    put("\n");
}

// A word of INITIALIZED data, and deliberately the first thing this file defines that has
// an initializer: under -n the data segment starts at a page boundary, and the exec path
// that reads it is the one that can lose its first word.  See the check below.
static int firstword = 0525252;

// More initialized data, to make the segment more than one word long, and a slot for the
// instance number the concurrent run passes in.
static int guard   = 0123456;
static int myid    = 0;
static char *lit   = "puret";

// bss: zero at entry, writable afterwards.
static int zero;
static int scratch[64];

int main(int argc, char **argv)
{
    int i;
    char *p;

    //
    // The layout the linker promised.  These are the three facts NMAGIC changes, and the
    // kernel's ts = pground(BADDR + btow(csize + tsize)) has to agree with all of them or
    // the data lands at the wrong virtual address.
    //
    ok("the image starts at word BADDR", (int)(unsigned)main >= 8);
    ok("text ends below data", (int)etext <= (int)edata);
    ok("data begins on a page boundary", ((int)etext & 01777) == 0);
    ok("bss ends above data", (int)end >= (int)edata);

    //
    // THE FIRST WORD OF THE DATA SEGMENT.  This is the check the program was written for.
    // getxfile() remaps the data region to read it, and if it reads it to virtual 0 the
    // first word is swallowed: a store to virtual word 0 is DROPPED and a load returns 0,
    // whatever page 0 is mapped to (kernel/README.md, "Never virtual page 0").  Under FMAGIC
    // the base is BADDR = 8 and the hole is never touched; under NMAGIC it is the very
    // first word of the segment, so exactly one initialized variable comes back zero and
    // nothing faults.  `firstword' is this file's first initializer, and the assertion
    // below is on its VALUE, not on its address -- if the compiler or the linker ever puts
    // something else at the origin, that word is the one at risk and this check goes with it.
    //
    ok("the first data word survived the load", firstword == 0525252);
    ok("the rest of data survived too", guard == 0123456);
    ok("a const string survived", lit[0] == 'p' && lit[4] == 't');

    //
    // bss, which is not in the file at all: the kernel clearsegs it and b6sim zero-fills.
    //
    ok("bss is zero at entry", zero == 0);
    for (i = 0; i < 64; i++)
        if (scratch[i] != 0)
            break;
    ok("a bss array is zero at entry", i == 64);

    //
    // Data and bss are WRITABLE -- both live in the private half of the image, however the
    // text is shared.  A kernel that mapped the shared text over them would fail here.
    //
    firstword = 0252525;
    zero      = 0777;
    for (i = 0; i < 64; i++)
        scratch[i] = i + 1;
    ok("data is writable", firstword == 0252525);
    ok("bss is writable", zero == 0777);
    for (i = 0; i < 64; i++)
        if (scratch[i] != i + 1)
            break;
    ok("a bss array is writable", i == 64);

    //
    // The break starts above bss, as it does for an impure image: the pure/impure split is
    // about the text, and sbrk() knows nothing about it.
    //
    ok("the break starts above bss", (int)(unsigned)sbrk(0) >= (int)end);

    //
    // The instance number, for the concurrent run.  Three copies of this program run at
    // once under kernel/test/swap, each with its own digit; the data segment is private, so
    // each must report its own.  With no argument the default stands, which is what keeps
    // one .expected file good for b6sim, libtest and the load test alike.
    //
    myid = '0';
    if (argc > 1 && argv[1] != 0 && argv[1][0] != 0)
        myid = argv[1][0];
    p = "id 0\n";
    // The literal is in the read-only region, so build the line in bss rather than patching
    // it -- on a machine with a real read-only text this would be the difference between a
    // test and a fault, and writing it the safe way documents which half is which.
    for (i = 0; p[i] != 0; i++)
        ((char *)scratch)[i] = p[i];
    ((char *)scratch)[3] = (char)myid;
    write(1, (char *)scratch, 5);

    put("done\n");
    return 0;
}
