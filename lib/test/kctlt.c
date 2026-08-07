//
// kctlt -- kctl(2) and kgetsym(3), the kernel-variable interface (kernel/kctl.c).
//
// THIS PROGRAM RUNS ON THE DISK IMAGE ONLY, for memt's reason and one more of its own: its
// subject is kernel state, and b6sim has no kernel -- it answers every kctl with ENOENT
// (cmd/sim/syscall.cpp), which is asserted there, in cmd/sim/test, and not here.
//
// The point of the exercise is that a table can be self-consistent and still be wrong.  So
// the strong checks are the ones that tie it to a fact the program already knows from
// somewhere else: its own pid, uid and parent, read out of the kernel's proc table; and
// u_procp, read out of the u-area through /dev/kmem, landing exactly on the entry the table
// says it should.  A table pointing at the wrong array, or at the right array with the wrong
// stride, passes every internal check and fails those.
//
// WHAT MAY REACH THE .expected FILE: verdicts only.  No address, no pid, no uid -- memt's
// rule, and every number this program learns is compared against one it already knows.
//
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

// The kernel's own view of itself, as memt reaches for it: <sys/proc.h> and <sys/user.h> for
// the two structures this program checks itself against, <sys/param.h> for the constants both
// sides compute a size from, and <sys/kctl.h> for the call.
#include <sys/kctl.h>
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/types.h>
#include <sys/user.h>

// A word in the hole between this program's break and the base of its stack.  Page 27 is the
// last one below USTKPAGE, and nothing maps it: an unallocated page has РП = 0 and its РЗ bit
// set (kernel/README.md), so a byte read there faults.  It is the only address that provokes
// EFAULT deterministically -- sbrk(0) does not, being the first byte past the break and still
// inside the break's own PAGE, which is mapped whole.  The first check below is that the page
// really is out of reach, so a program that grew into it says so instead of passing.
#define BADWORD ((USTKPAGE - 1) * PGSZ)

// Fabricating a char * from an int is kernel/dev/mem.c's conversion and has to be spelled its
// way: int -> int * copies the word address, int * -> char * sets the fat marker and byte
// offset.  A direct (char *) of an int would build the marker bit wrong (kernel task 34).
#define BADPTR ((char *)(int *)BADWORD)

#define NLIST 64 // room for more names than the table has, so a full LIST is never clipped

static struct proc ptab[NPROC]; // bss, not a frame: 1800 words is half the stack
static struct psinfo psi[NPROC];
static char list[NLIST * KSYMLEN];

static void ok(const char *what, int cond)
{
    printf("%s %s\n", cond ? "ok  " : "FAIL", what);
}

int main(void)
{
    struct kctlstat st, st2;
    struct user ku;
    int km, i, n, self, a_msgbuf, mp;

    // The premise of the two EFAULT checks, asserted rather than assumed.
    ok("the page below the stack is not ours", ptrword(sbrk(0)) < BADWORD);

    // ---- KCTL_STAT ---------------------------------------------------------------------
    n = kctl("proc", KCTL_STAT, &st, sizeof st);
    ok("a stat of proc comes back whole", n == (int)sizeof st);
    ok("its address is inside the kernel", st.kc_addr > 0 && st.kc_addr < KEND);
    // The kernel's sizeof against ours.  Both sides compute it from the same <sys/param.h>
    // and the same struct, so a mismatch means the table was written with a literal.
    ok("its size is the whole proc table", st.kc_size == NPROC * (int)sizeof(struct proc));
    ok("it is readable and not writable", st.kc_flags == KCTLF_RD);

    // kgetsym(3) is the same answer with the struct spelled out for you.
    ok("kgetsym agrees with the stat", kgetsym("proc") == st.kc_addr);

    // ---- KCTL_GET, against facts this process already knows ------------------------------
    n = kctl("proc", KCTL_GET, ptab, sizeof ptab);
    ok("the proc table reads back whole", n == (int)sizeof ptab);
    self = -1;
    for (i = 0; i < NPROC; i++)
        if (ptab[i].p_stat != 0 && ptab[i].p_pid == getpid()) {
            self = i;
            break;
        }
    ok("we are in it", self >= 0);
    ok("under our own uid", self >= 0 && ptab[self].p_uid == getuid());
    ok("with our own parent", self >= 0 && ptab[self].p_ppid == getppid());

    // ---- the address, checked through a second route ------------------------------------
    // u_procp is the running process's own proc entry, and the u-area is at a FIXED address
    // (memt's point), so this needs no lookup at all.  It must land exactly on the slot the
    // table's base address and the struct's stride put us at: a base off by an array, or a
    // stride off by a word, fails here and nowhere above.
    km = open("/dev/kmem", 0);
    ok("/dev/kmem opens", km >= 0);
    if (km < 0 || lseek(km, (off_t)UBASE * NBPW, 0) < 0 || read(km, &ku, sizeof ku) != sizeof ku)
        ok("u_procp is the slot the table's base points at", 0);
    else
        ok("u_procp is the slot the table's base points at",
           self >= 0 && (int)ku.u_procp == st.kc_addr + self * (int)(sizeof(struct proc) / NBPW));
    if (km >= 0)
        close(km);

    // ---- sizing, and the short read ------------------------------------------------------
    // A len of 0 copies nothing and answers with what is there; a short len truncates and
    // says how much came, exactly as read(2) does.  Nothing else reports a size.
    ok("a len of 0 reports the size", kctl("proc", KCTL_GET, 0, 0) == st.kc_size);
    ok("a short buffer is filled and no more", kctl("proc", KCTL_GET, ptab, 60) == 60);

    // ---- the dmesg pair -------------------------------------------------------------------
    a_msgbuf = kgetsym("msgbuf");
    ok("msgbuf is inside the kernel", a_msgbuf > 0 && a_msgbuf < KEND);
    n  = kctl("msgbufp", KCTL_GET, &mp, sizeof mp);
    mp = ptrword(mp); // it is a FAT char *; the word address is the low 15 bits
    ok("msgbufp reads back whole", n == (int)sizeof mp);
    ok("and points into msgbuf", mp >= a_msgbuf && mp < a_msgbuf + btow(MSGBUFS));

    // ---- KCTL_LIST ------------------------------------------------------------------------
    n = kctl(0, KCTL_LIST, list, sizeof list);
    ok("the list is a whole number of records", n > 0 && n % KSYMLEN == 0);
    ok("a len of 0 reports its size too", kctl(0, KCTL_LIST, 0, 0) == n);
    // Every name in it is terminated inside its record and names something in the kernel.
    // This is the honesty check: it needs no list of its own to compare against, so it
    // cannot fall out of step with the table the way a checked-in copy would.
    self = 1;
    for (i = 0; i < n / KSYMLEN; i++) {
        char *nm = &list[i * KSYMLEN];
        int j;
        for (j = 0; j < KSYMLEN && nm[j]; j++)
            ;
        if (j == KSYMLEN) { // no NUL in the record
            self = 0;
            break;
        }
        if (kctl(nm, KCTL_STAT, &st2, sizeof st2) != (int)sizeof st2 || st2.kc_addr <= 0 ||
            st2.kc_addr >= KEND || st2.kc_size <= 0) {
            self = 0;
            break;
        }
    }
    ok("and every name in it stats to something inside the kernel", self);

    // ---- the refusals ----------------------------------------------------------------------
    ok("an unknown name is ENOENT",
       kctl("no_such_var", KCTL_STAT, &st2, sizeof st2) < 0 && errno == ENOENT);
    ok("an empty name is ENOENT too, and not special",
       kctl("", KCTL_STAT, &st2, sizeof st2) < 0 && errno == ENOENT);
    ok("a name with no NUL in KSYMLEN bytes is EINVAL",
       kctl("aaaaaaaaaaaaaaaa", KCTL_STAT, &st2, sizeof st2) < 0 && errno == EINVAL);
    ok("KCTL_SET is reserved and refused",
       kctl("proc", KCTL_SET, &st2, sizeof st2) < 0 && errno == EINVAL);
    ok("so is an operation that does not exist",
       kctl("proc", 99, &st2, sizeof st2) < 0 && errno == EINVAL);
    ok("an unreadable name is EFAULT",
       kctl(BADPTR, KCTL_STAT, &st2, sizeof st2) < 0 && errno == EFAULT);
    ok("an unwritable buffer is EFAULT",
       kctl("proc", KCTL_STAT, BADPTR, sizeof st2) < 0 && errno == EFAULT);

    // ---- KCTL_PSINFO ----------------------------------------------------------------------
    // The other operation that names nothing.  What is checkable without a second process is
    // the SHAPE and the JOIN: NPROC records in proc[]'s order, ours among them, carrying the
    // name and the terminal the rest of this suite already knows independently.
    n = kctl(0, KCTL_PSINFO, psi, sizeof psi);
    ok("psinfo fills one record per proc slot", n == (int)sizeof psi);
    ok("its len of 0 reports a size as well", kctl(0, KCTL_PSINFO, 0, 0) == n);
    ok("and a short buffer truncates the same way", kctl(0, KCTL_PSINFO, psi, 60) == 60);

    // Re-read whole, then join against the proc table by index -- which is the contract, and
    // the one thing a caller cannot check any other way.
    kctl(0, KCTL_PSINFO, psi, sizeof psi);
    self = -1;
    for (i = 0; i < NPROC; i++)
        if (ptab[i].p_stat != 0 && ptab[i].p_pid == getpid())
            self = i;
    ok("our slot has our pid in both tables", self >= 0 && psi[self].ps_pid == getpid());
    ok("and our own command name", self >= 0 && psi[self].ps_comm[0] == 'k' &&
                                       psi[self].ps_comm[1] == 'c' && psi[self].ps_comm[2] == 't');
    ok("on a terminal the kernel can name", self >= 0 && psi[self].ps_ttyn >= 0 &&
                                                psi[self].ps_ttyn < NSC);
    // An empty proc slot carries no u-area, and says so the one way the interface allows.
    n = -1;
    for (i = 0; i < NPROC; i++)
        if (ptab[i].p_stat == 0) {
            n = i;
            break;
        }
    ok("an unused slot comes back empty", n < 0 || (psi[n].ps_time == 0 && psi[n].ps_ttyn < 0 &&
                                                    psi[n].ps_comm[0] == '\0'));
    ok("a null name is fine for it", kctl(0, KCTL_PSINFO, psi, sizeof psi) == (int)sizeof psi);

    // ---- the arity canary --------------------------------------------------------------------
    // sy_narg is the ONE thing about a new system call that fails silently: the gate pops
    // n-1 words on the caller's behalf, so a count that disagrees with the prototype drifts
    // the user stack a word per call and corrupts a frame some hundreds of calls later.
    // Nothing else in this suite makes a four-argument call at all.
    {
        volatile int below = 0525252, above = 0123456;
        for (i = 0; i < 200; i++)
            kctl("proc", KCTL_STAT, &st2, sizeof st2);
        ok("200 calls leave the frame alone", below == 0525252 && above == 0123456);
    }

    return 0;
}
