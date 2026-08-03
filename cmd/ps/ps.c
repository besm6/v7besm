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
// WHAT REPLACES ALL OF IT.  kctl(2) hands over the whole proc table by name and value in one
// call (<sys/kctl.h>), kgetsym(3) gives the word address of sc[] so a tty pointer can be
// turned into an index, and u_comm -- which is in the u-area on this system (<sys/user.h>)
// -- IS the command name, so the scavenger is not needed at all.  lib/test/memt.c and
// lib/test/kctlt.c are the worked examples of the ladder and were written before this
// program to prove it.  v7's OUTPUT FORMAT is kept, and so is its ps.1.
//
// THE U-AREA IS IN ONE OF THREE PLACES, and getting that wrong is the way to print numbers
// that are quietly one context switch old:
//
//   p_addr == uhome.  The LIVE u-area at UBASE is this process's, and the copy in its image
//     at p_addr is stale -- uflush() has not run for it yet (kernel/text.c).  Read UBASE
//     through /dev/kmem, which is what lib/test/memt.c does.  `uhome' is exported for
//     exactly this and for nothing else.
//
//   SLOAD set, p_addr != uhome.  The image is in core and its first USIZE words are the
//     saved u-area.  p_addr is at or above KREACH, out of /dev/kmem's reach, so this one
//     goes through /dev/mem.
//
//   SLOAD clear.  The process is swapped out and p_addr is a block on the paging store, not
//     an address.  v7 read it back off /dev/swap; this one does not, and the row prints its
//     proc[] columns with the u-area ones blank.  A swap reader is a lot of program for a
//     process that is by definition not doing anything.
//
// THE TTY COLUMN DOES NOT SCAN /dev.  v7 read the whole directory as a raw struct direct
// stream and matched st_rdev against u_ttyd.  Here sc[] is the only terminal array in the
// kernel (kernel/dev/sc.c) and sc[minor(dev)] is how the driver itself indexes it, so the
// index of u_ttyp in sc[] IS the terminal's name, and it costs one kgetsym.  That also
// makes this program work under b6sim, which cannot read a directory descriptor at all
// (../README.md SS9).
//
// FOUR COLUMNS CHANGED, all of them because the machine did; ps.1 says so too:
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
// NOT SETUID, AND IT MUST NOT BECOME ONE.  /dev/kmem and /dev/mem are mode 0640 and root's,
// so this program works for root and reports "no memory device" for anybody else -- exactly
// as v7's did.  A setuid ps would hand out every process's memory through a program that
// already knows the layout; ../quot/CMakeLists.txt makes the same argument about /dev/rmd0.
//
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <fcntl.h>
#include <sys/kctl.h>
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/tty.h>
#include <sys/types.h>
#include <sys/user.h>

// bss, not frames.  The proc table is NPROC * 12 words -- 1,800 of them -- which is nearly
// half the 4,096-word stack, and nothing checks that ceiling (../README.md SS6).
// lib/test/kctlt.c does the same and for the same reason.
static struct proc ptab[NPROC];
static struct user ku;

static int aflg, xflg, lflg;
static int chkpid;
static int uhome, a_sc;
static int kmem = -1, memf = -1;

// v7's, indexed by p_stat: SSLEEP SWAIT SRUN SIDL SZOMB SSTOP (<sys/proc.h>).
static char states[] = "0SWRIZT";

//
// Read n bytes from a kernel/physical word address.  Both devices take a BYTE offset and
// word W is at W * NBPW (kernel/dev/mem.c); lib/test/memt.c's rdwords() is the same helper
// one word at a time.
//
static int rdwords(int fd, int w, void *buf, int n)
{
    if (fd < 0)
        return -1;
    if (lseek(fd, (off_t)w * NBPW, 0) < 0)
        return -1;
    return read(fd, buf, n) == n ? 0 : -1;
}

//
// Fetch p's u-area into ku.  Returns 0, or -1 when there is none to be had.  See the header:
// the live copy wins over the image, and a swapped-out process has neither.
//
static int getu(struct proc *p)
{
    if ((int)p->p_addr == uhome)
        return rdwords(kmem, UBASE, &ku, sizeof ku);
    if ((p->p_flag & SLOAD) == 0)
        return -1;
    return rdwords(memf, (int)p->p_addr, &ku, sizeof ku);
}

//
// The terminal's name: the index of u_ttyp in sc[].  "?" when the process has no controlling
// terminal, or when the pointer is not one of sc[]'s.
//
static char *ttyname_of(int haveu)
{
    static char buf[4];
    int i;

    if (!haveu || ku.u_ttyp == NULL)
        return "?";
    i = (ptrword(ku.u_ttyp) - a_sc) / (int)(sizeof(struct tty) / NBPW);
    if (i < 0 || i >= NSC)
        return "?";
    buf[0] = '0' + i;
    buf[1] = '\0';
    return buf;
}

//
// One row.  Returns 1 if it printed anything, which is v7's protocol for "put a newline
// under it".
//
static int prcom(struct proc *p)
{
    int haveu, tm;

    // A ZOMBIE'S U-AREA IS NOT READ AT ALL, and the order matters here where it did not in
    // v7.  v7 read the u-area first and tested SZOMB twenty lines later; on this kernel
    // exit() calls mfree(coremap, p_size, p_addr) and only THEN sets p_stat to SZOMB
    // (kernel/sys1.c), so a zombie's p_addr names core that has already been handed back and
    // may hold somebody else's image.  Reading it would print another process's times under
    // this one's pid.
    haveu = p->p_stat != SZOMB && getu(p) == 0;

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

    printf(" %-2.2s", ttyname_of(haveu));

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
    tm = (ku.u_utime + ku.u_stime + HZ / 2) / HZ;
    printf(" %2d:", tm / 60);
    tm %= 60;
    printf(tm < 10 ? "0%d" : "%d", tm);

    // u_comm is char[DIRSIZ] and is NOT NUL-terminated when the name fills it, so the
    // precision is what bounds it -- ../README.md SS5, and dcheck and ncheck do the same.
    printf(" %.*s", DIRSIZ, ku.u_comm);
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

    n = kctl("proc", KCTL_GET, ptab, sizeof ptab);
    if (n != (int)sizeof ptab) {
        fputs("ps: cannot read the proc table\n", stderr);
        return 1;
    }
    if (kctl("uhome", KCTL_GET, &uhome, sizeof uhome) != (int)sizeof uhome ||
        (a_sc = kgetsym("sc")) < 0) {
        fputs("ps: this kernel exports no uhome or sc\n", stderr);
        return 1;
    }

    // Both nodes are mode 0640 and root's, so this is where a non-root ps stops -- v7's
    // said "No mem" and stopped here too.
    kmem = open("/dev/kmem", O_RDONLY);
    memf = open("/dev/mem", O_RDONLY);
    if (kmem < 0 || memf < 0) {
        fputs("ps: no memory device (ps is for the super-user)\n", stderr);
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
        if (prcom(p)) {
            printf("\n");
            retcode = 0;
        }
    }
    return retcode;
}
