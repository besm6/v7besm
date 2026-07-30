//
// memt -- /dev/null, /dev/kmem and /dev/mem (kernel/dev/mem.c, task 27).
//
// THIS PROGRAM RUNS ON THE DISK IMAGE ONLY, never under b6sim, and unlike its neighbours
// here it is not about libc: it is the user-mode half of the memory driver's test, and the
// libc suite is where it can be run off /usr/test by a booted kernel for the price of one
// b6_libtest() call.  kernel/test/mmutest covers the other half -- copyphys(), the mapped
// bracket underneath /dev/mem -- against the bare machine.
//
// IT NEEDS NO LINKER SYMBOL, and that is the whole design of it.  The u-area sits at a
// FIXED physical address (UBASE, <sys/param.h>), so /dev/kmem can be aimed at this
// process's own `struct user' with nothing but a header constant; u_procp then names its
// proc entry, whose p_pid this program can check against getpid() and whose p_addr is the
// physical page its own image lives on.  That is `ps''s route, and it is what turns a
// /dev/mem read into an assertion instead of a round trip: the word it asks for is above
// 0100000, out of reach of any unmapped access, and only the right address holds `magic'.
//
// Then it WRITES that word through /dev/mem and reads the variable back through its own
// address space -- two entirely different paths to one word of memory.  A driver whose
// БРЗ drains were wrong would pass every read here and fail that.
//
// WHAT MAY REACH THE .expected FILE: verdicts only.  No pid, no address, no uid -- every
// number this program learns is compared against one it already knows.
//
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

// The kernel's own view of itself.  <sys/user.h> and <sys/proc.h> have never been included
// by a user program before this one; each now includes what it needs, so the order below is
// only alphabetical.  param.h and types.h are named because this file uses them too --
// UBASE, USIZE, NBPW and off_t -- not to prop the other two up.
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/types.h>
#include <sys/user.h>

#define MAGIC1 0525252 // what this program plants in `magic'
#define MAGIC2 0123456 // what the kernel is asked to put there instead

// The word the last two checks are about.  Volatile so that the read after the /dev/mem
// write is a real load: the whole point is that the value changed under the program.
static volatile int magic = MAGIC1;

// A stable stretch of the kernel image to compare the two devices over: the const segment,
// which begins at word BADDR and is never written.  Word 0100 is well inside it.
#define STABLE 0100
#define NCMP   16

static void ok(const char *what, int cond)
{
    printf("%s %s\n", cond ? "ok  " : "FAIL", what);
}

// Read n words at word address w.  Returns the byte count read, so a short read or an EOF
// is visible to the caller rather than hidden.
static int rdwords(int fd, int w, int *buf, int n)
{
    if (lseek(fd, (off_t)w * NBPW, 0) < 0)
        return -1;
    return read(fd, buf, n * sizeof(int));
}

int main(void)
{
    struct user ku; // this process's u-area, read back through /dev/kmem
    struct proc pr; // ...and the proc entry it points at
    int mbuf[NCMP], kbuf[NCMP];
    int null, km, mm;
    int i, w, pa, n;
    int found;

    // /dev/null -- minor 2, the one that already worked.
    null = open("/dev/null", 2);
    ok("/dev/null opens", null >= 0);
    ok("a read of it is at EOF", read(null, kbuf, sizeof kbuf) == 0);
    ok("a write of it is swallowed whole", write(null, kbuf, 30) == 30);
    close(null);

    // /dev/kmem -- minor 1.  Byte offsets, so word W is at W * NBPW.
    km = open("/dev/kmem", 0);
    ok("/dev/kmem opens", km >= 0);
    if (lseek(km, (off_t)UBASE * NBPW, 0) < 0 || read(km, &ku, sizeof ku) != sizeof ku) {
        ok("the u-area reads back whole", 0);
        return 1;
    }
    ok("the u-area reads back whole", 1);

    // Three things this process knows about itself, answered by the kernel's copy.
    ok("its u_uid is our uid", ku.u_uid == getuid());
    ok("its u_gid is our gid", ku.u_gid == getgid());
    ok("this image is impure, so virtual 0 is p_addr + USIZE", ku.u_tsize == 0);

    // u_procp is a kernel word address; follow it.  Nothing but the right address holds a
    // proc entry whose p_pid is ours.
    if (rdwords(km, (int)ku.u_procp, (int *)&pr, sizeof pr / sizeof(int)) != (int)sizeof pr) {
        ok("u_procp names our proc entry", 0);
        return 1;
    }
    ok("u_procp names our proc entry", pr.p_pid == getpid());
    ok("our image lives above the unmapped reach", pr.p_addr >= KREACH);

    // /dev/mem -- minor 0.  Below KREACH it is the same memory /dev/kmem serves, by a
    // different path in the driver: the two must agree word for word over a stretch that
    // does not change (the kernel's const segment).
    mm = open("/dev/mem", 2);
    ok("/dev/mem opens", mm >= 0);
    n     = rdwords(km, STABLE, kbuf, NCMP);
    found = rdwords(mm, STABLE, mbuf, NCMP);
    ok("both devices read the low words", n == (int)sizeof kbuf && found == n);
    for (i = 0; i < NCMP; i++)
        if (kbuf[i] != mbuf[i])
            break;
    ok("and they agree over the kernel's const segment", i == NCMP);

    // A read that STRADDLES the boundary between the two paths: the last word an unmapped
    // access can name, and the first one that needs a window.  Both come back, and the
    // first is the word /dev/kmem sees at the same offset.  (The second belongs to the
    // page pool and is nobody's to predict, so it is not compared.)
    n = rdwords(mm, KREACH - 1, mbuf, 2);
    ok("a read crossing KREACH returns both words", n == 2 * (int)sizeof(int));
    ok("and its first word is the one kmem shows",
       rdwords(km, KREACH - 1, kbuf, 1) == (int)sizeof(int) && kbuf[0] == mbuf[0]);

    // Off the end: /dev/kmem stops at the unmapped reach, /dev/mem at the size of core.
    ok("/dev/kmem ends at the unmapped reach", rdwords(km, KREACH, kbuf, 1) == 0);
    ok("/dev/mem ends at the end of core", rdwords(mm, 4 * 512 * 1024, kbuf, 1) == 0);

    // THE ONE THAT NEEDS THE WINDOW.  This process's virtual word V is physical
    // p_addr + USIZE + V -- the u-area page, then the image, as sureg() lays it out
    // (kernel/utab.c) -- and `magic' is above 0100000 there.  Only that address holds
    // MAGIC1; a driver off by a word, a page, or the whole u-area reads something else.
    pa    = pr.p_addr + USIZE + (int)&magic;
    found = rdwords(mm, pa, &w, 1) == (int)sizeof(int) && w == MAGIC1;
    ok("/dev/mem finds our own word above 0100000", found);

    // ...and writing it there changes the variable under us.  Guarded on the read above,
    // so a wrong address is never written to.
    if (found) {
        w = MAGIC2;
        if (lseek(mm, (off_t)pa * NBPW, 0) < 0 || write(mm, &w, sizeof w) != sizeof w)
            ok("a write through /dev/mem reaches it", 0);
        else
            ok("a write through /dev/mem reaches it", magic == MAGIC2);
    } else
        ok("a write through /dev/mem reaches it", 0);

    close(km);
    close(mm);
    return 0;
}
