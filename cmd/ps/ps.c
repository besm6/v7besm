/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// ps -- process status.
//
//      ps [ alx ] [ pid ]
//
// Task C8's fourth, and THE ONE THAT IS NOT A PORT.  ../TODO.md says so and this is the
// account of why.  v7's ps.c is 408 lines of which perhaps forty are the report; the rest
// is a route to the data that has no counterpart here:
//
//   it nlist()s /unix, and there is no /unix on this image -- ../../root.manifest names no
//   kernel and kernel/unix.ini has the SIMULATOR load one off the build host;
//   it then applies a hard-coded relocation fudge, `n_value - 0x7fc00000 + 0x10000';
//   it reads the proc table by sequential read() off /dev/mem;
//   it fetches each u-area either at ctob(p_addr) or, for a swapped process, at
//     (p_addr + swplo) << 9 in /dev/swap, which is PDP-11 memory management throughout;
//   and it reconstructs the command line by walking the process's stack through a
//     two-segment address map, a scavenger it warns about in its own manual page.
//
// WHAT REPLACES ALL OF IT: two kctl(2) calls and nothing else (<sys/kctl.h>).  KCTL_GET of
// `proc' hands over the whole table by value; KCTL_PSINFO hands over the four u-area fields
// per slot, the command name among them -- so the stack scavenger is not needed and neither
// is a memory device.  lib/test/kctlt.c is the worked example.  v7's OUTPUT FORMAT is kept,
// and so is its ps.1.
//
// THE U-AREA IS THE KERNEL'S PROBLEM NOW.  This program used to reach it itself: the live
// copy at UBASE through /dev/kmem, a saved one at p_addr through /dev/mem, and nothing at
// all for a process swapped out.  That three-place rule is still the rule and is still
// written down -- in kernel/kctl.c, where uhome is a variable rather than a table row and
// p_addr is reached with copyphys() rather than through a device.  KCTL_PSINFO returns the
// four fields; a swapped-out process still prints <swapped>, which is what it always did.
//
// THE TTY COLUMN DOES NOT SCAN /dev.  v7 read the whole directory as a raw struct direct
// stream and matched st_rdev against u_ttyd.  sc[] is the only terminal array in the kernel
// and sc[minor(dev)] is how the driver itself indexes it, so the subscript IS the terminal's
// name -- computed in the kernel now, and never written down.  That is also what makes this
// program work under b6sim, which cannot read a directory descriptor (../README.md SS9).
//
// FOUR COLUMNS CHANGED, all of them because the machine did; ps.1.umm says so too:
//   ADDR   a WORD address printed in octal, not a click address in hex.
//   SZ     WORDS, a multiple of PGSZ, not 64-byte blocks.
//   WCHAN  octal, and the whole of a thin chan_t rather than v7's 24-bit mask.
//   TIME   u_utime + u_stime are ticks at HZ, which is 250 here and was 60 there.
//
// THE SEVENTEEN longs ARE GONE.  ../README.md SS3 counts them and this file had the most of
// any source in the tree; a long is one 41-bit word here and %ld is parsed-and-ignored, so
// every one of them is a plain int and every conversion is %d.  v7's `%D' would have been
// worse: it prints the two characters %D and consumes no argument.
//
// NOT SETUID, AND IT NEVER WAS -- AND SINCE KCTL_PSINFO IT NEEDS NOTHING TO BE.  It used to
// open /dev/kmem and /dev/mem, both mode 0640 and root's, for four fields of a u-area, and
// so worked for root and told everybody else to go away.  The fix was not to loosen those
// modes and not to borrow a uid; it was to stop asking for memory.  A setuid ps would hand
// out every process's MEMORY through a program that already knows the layout -- the argument
// ../quot/CMakeLists.txt makes about /dev/rmd0 -- and it is still true, which is why the
// call returns a digest and not an address.  ../README.md SS8 is the rule.
//
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/kctl.h>
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/types.h>

// bss, not frames.  proc[] is NPROC * 12 words and psinfo[] NPROC * 6 -- 2,700 together,
// against a 4,096-word stack that nothing checks (../README.md SS6).
static struct proc ptab[NPROC];
static struct psinfo psi[NPROC];

static int aflg, xflg, lflg;
static int chkpid;

// v7's, indexed by p_stat: SSLEEP SWAIT SRUN SIDL SZOMB SSTOP (<sys/proc.h>).
static char states[] = "0SWRIZT";

// The terminal's name: the index the kernel put in ps_ttyn, or "?" for none.
static char *ttyname_of(struct psinfo *ps)
{
    static char buf[4];

    if (ps->ps_ttyn < 0)
        return "?";
    buf[0] = '0' + ps->ps_ttyn;
    buf[1] = '\0';
    return buf;
}

//
// One row.  Returns 1 if it printed anything, which is v7's protocol for "put a newline
// under it".
//
static int prcom(struct proc *p, struct psinfo *ps)
{
    int haveu, tm;

    // ps_pid is the join key: a process can exit and its slot be reused between the two
    // calls, so a row that does not match is one this table never described.  The zombie and
    // swapped-out cases the kernel already left empty (<sys/kctl.h>).
    haveu = ps->ps_pid == p->p_pid && ps->ps_comm[0] != '\0';

    if (lflg) {
        printf("%2o %c%4d", p->p_flag, states[(int)p->p_stat & 07], p->p_uid);
        printf("%6d", p->p_pid);
        printf("%6d%4d%4d%5d%8o%7d", p->p_ppid, p->p_cpu & 0377, p->p_pri, p->p_nice,
               (int)p->p_addr, p->p_size);
        if (p->p_wchan)
            printf("%8o", (int)p->p_wchan);
        else
            printf("        ");
    } else
        printf("%6d", p->p_pid);

    printf(" %-2.2s", ttyname_of(ps));

    if (p->p_stat == SZOMB) {
        printf("  <defunct>");
        return 1;
    }
    if (!haveu) {
        // Swapped out: no u-area, so no times and no name.  v7 went to /dev/swap for both.
        printf("      <swapped>");
        return 1;
    }

    // v7 rounded to the nearest second at 60 ticks; HZ is 250 here.
    tm = (ps->ps_time + HZ / 2) / HZ;
    printf(" %2d:", tm / 60);
    tm %= 60;
    printf(tm < 10 ? "0%d" : "%d", tm);

    // ps_comm is char[DIRSIZ] and is NOT NUL-terminated when the name fills it, so the
    // precision is what bounds it -- ../README.md SS5, and dcheck and ncheck do the same.
    printf(" %.*s", DIRSIZ, ps->ps_comm);
    return 1;
}

static void usage(void)
{
    fputs("usage: ps [ alx ] [ pid ]\n", stderr);
    exit(1);
}

int main(int argc, char *argv[])
{
    char *ap;
    int i, n, uid, retcode = 1;

    if (argc > 1) {
        ap = argv[1];
        if (*ap == '-')
            ap++;
        while (*ap) {
            switch (*ap++) {
            case 'a':
                aflg++;
                break;
            case 'x':
                xflg++;
                break;
            case 'l':
                lflg++;
                break;
            default:
                // v7 took a bare number here as "report only this process".  It also
                // accepted `k' (a /usr/sys/core that does not exist), `v' and a `t' that
                // dereferenced a null pointer when given alone; all three are gone.
                if (ap[-1] >= '0' && ap[-1] <= '9') {
                    chkpid = atoi(ap - 1);
                    aflg   = xflg = 1;
                    ap     = "";
                    break;
                }
                usage();
            }
        }
    }

    // Two calls, neither privileged.  A literal 0 for the name: KCTL_PSINFO does not look
    // at it (lib/test/kctlt.c is the idiom).
    n = kctl("proc", KCTL_GET, ptab, sizeof ptab);
    if (n != (int)sizeof ptab) {
        fputs("ps: cannot read the proc table\n", stderr);
        return 1;
    }
    if (kctl(0, KCTL_PSINFO, psi, sizeof psi) != (int)sizeof psi) {
        fputs("ps: this kernel has no KCTL_PSINFO\n", stderr);
        return 1;
    }

    uid = getuid();
    if (lflg)
        printf(" F S UID   PID  PPID CPU PRI NICE    ADDR     SZ   WCHAN TTY TIME CMD\n");
    else if (chkpid == 0)
        printf("   PID TTY TIME CMD\n");

    for (i = 0; i < NPROC; i++) {
        struct proc *p = &ptab[i];

        // v7's selection, unchanged: an empty slot; then the system processes, which have
        // no process group and are root's, unless -x; then everybody else's, unless -a;
        // then everything but the named pid, if one was named.
        if (p->p_stat == 0)
            continue;
        if (p->p_pgrp == 0 && xflg == 0 && p->p_uid == 0)
            continue;
        if ((uid != p->p_uid && aflg == 0) || (chkpid != 0 && chkpid != p->p_pid))
            continue;
        if (prcom(p, &psi[i])) {
            printf("\n");
            retcode = 0;
        }
    }
    return retcode;
}
