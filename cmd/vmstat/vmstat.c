/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

//
// vmstat -- report virtual-memory and system-activity statistics.
//
//      vmstat [ -p ] [ interval [ count ] ]
//      vmstat -s
//
// NOT A v7 PROGRAM.  v7 has no vmstat; this is 2.11BSD's, by way of RetroBSD's
// (src/cmd/vmstat.c), and it is here because kernel/ksym.c had been holding a dozen live
// counters open for it -- "they come with a vmstat", the table's own closing line.  They
// came with this one, and so did the driver instrumentation that had been outstanding beside
// them: dev/md.c and dev/mb.c now keep dk_busy, dk_numb[] and dk_wds[], which is where the
// two device columns come from and why iostat has a disk half at last.
//
// WHAT THE SOURCE ASKED FOR AND THIS SYSTEM DOES NOT HAVE, since a port that quietly drops
// a column is worse than one that says which:
//
//   struct vmtotal, struct vmrate, struct vmsum, struct forkstat, struct nchstats -- none of
//   them exists here, and the kernel that would fill them does not either.  What replaces
//   the first is a SCAN OF proc[] AND coremap IN USER SPACE: RetroBSD's kernel recomputes
//   `total' every five seconds in vmtotal() and a vmstat reads the answer, which is a
//   five-second-stale answer bought with kernel code.  Both tables are already exported for
//   ps and pstat, the scan is 150 rows, and doing it here costs the kernel nothing and is
//   never stale.  There is no name cache on this system at all, so -s has no lookup line.
//
//   _boottime and _hz -- not needed, and this is the substantive simplification.  clock()
//   reaches its dk_time[a] += 1 on EVERY path, so the sum of the 32 slots is ticks accounted
//   since boot: uptime is that over HZ, and HZ is a <sys/param.h> constant user code can
//   see.  Using it rather than a wall clock also puts every number in a report over the SAME
//   denominator, so the rates and the percentages cannot disagree; and the first report and
//   the later ones then share one formula, where BSD needed a `nintv != 1' branch and a
//   second kernel structure to read.  What it measures is ticks ACCOUNTED: the timer is a
//   flip-flop and free-runs, so a tick that arrives while delivery is blocked is coalesced,
//   and this is a lower bound on wall clock.  iostat prints under the same convention.
//
//   _dk_ndrive, _dk_name[], _dk_unit[] -- kernel/ksym.c's doctrine is that a constant lives
//   in <sys/param.h>, where user code can see it, rather than in a table row.  NDK is 2 and
//   the two devices are DK_MD and DK_MB; there is nothing to read out of the kernel and no
//   two-level dereference through /dev/kmem to do it with.
//
//   _freemem -- RetroBSD declares it and ASSIGNS IT NOWHERE, so its `fre' column is always
//   zero.  Here it is the sum of coremap's free extents, in words, which is a real number.
//
// FOUR FLAGS ARE GONE, and none of them silently:
//
//   -t and -i printed "not applicable to 2.11BSD" and exited.  They are not applicable here
//   either and the way to say so is not to have them.
//   -z zeroed the counters by writing /dev/kmem.  kctl(2) has a KCTL_SET and REFUSES it:
//   nothing needs to write a kernel variable yet, and when something does it wants a
//   per-entry KCTLF_WR and a suser() gate (<sys/kctl.h>).  The differential report is what
//   -z was mostly wanted for and it is the ordinary path here.
//   -f reported fork accounting.  There is none: `mpid' is a fork count only until it wraps,
//   and a real one would be two more counters nothing else asked for.
//   signal(SIGCONT, printhdr) is gone with them -- this kernel's signals stop at 15.
//
// THE ARITHMETIC IS INTEGER, cmd/iostat's account and cmd/iostat's idiom: no -lm, no IEEE
// 754, and pct() and persec() halve both sides until the multiply fits in a 41-bit int
// rather than dividing first and losing the digit always.  The four CPU percentages are
// rounded independently and may sum to 99 or 101; BSD's do too.
//
// EVERY CPU CATEGORY IS THE SUM OF EIGHT SLOTS.  kernel/clock.c bills a tick to
// dk_time[(dk_busy & 07) + state*8], and dk_busy is live, so the four bases are bases and
// not slots.  cmd/iostat/iostat.c is the long version.
//
// NOT SETUID, and in /bin as 2.11BSD had it: it opens no device and needs no privilege --
// kctl(2) is unprivileged -- so it is an ordinary user command and its page is section 1.
// iostat and pstat are in /etc because v7's pages for THEM are section 1M, which is a fact
// about v7 rather than about privilege; cmd/dmesg made the same move for the same reason.
//
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/kctl.h>
#include <sys/map.h>
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/text.h>

// The base of each CPU group in dk_time[], and the width of a group.
#define O_USER 0
#define O_NICE 8
#define O_SYS  16
#define O_IDLE 24
#define NCPUST 8

// The rate counters, in the order -s prints them.  A row is one kctl(2) scalar and one
// label; the periodic report indexes this array by name below, which is why the order is
// fixed here rather than in each printf.
#define R_SWPIN    0
#define R_SWPOUT   1
#define R_TEXTIN   2
#define R_TEXTOUT  3
#define R_TEXTJOIN 4
#define R_SWTCH    5
#define R_INTR     6
#define R_TRAP     7
#define R_SYSCALL  8
#define R_IOBULK   9
#define R_IOEDGE   10
#define R_IOSHIFT  11
#define NRATE      12

static char *rname[] = {
    "nswapin", "nswapout", "ntextin",  "ntextout", "ntextjoin", "nswtch",
    "nintr",   "ntrap",    "nsyscall", "niobulk",  "nioedge",   "nioshift",
};

// What -s calls them.  Separate from the kernel's spelling on purpose: the table above is a
// list of variables and this is a list of sentences, and neither should constrain the other.
static char *rlabel[] = {
    "swap ins",
    "swap outs",
    "text images read in",
    "text images written out",
    "joins to a text already in core",
    "cpu context switches",
    "device interrupts",
    "traps",
    "system calls",
    "bytes copied whole words at a time",
    "bytes copied a byte at a time, in phase",
    "bytes copied a byte at a time, out of phase",
};

// The tables, all at file scope.  The stack is 4,096 words and proc[NPROC] alone is 1,800;
// cmd/pstat says the same thing about the same arrays.
static struct proc xproc[NPROC];
static struct text xtext[NTEXT];
static struct map xcore[CMAPSIZ];

static int cur[NDKTIME], prev[NDKTIME], delta[NDKTIME];
static int numb[NDK], pnumb[NDK], dnumb[NDK];
static int rcur[NRATE], rprev[NRATE], rdelta[NRATE];

static int pflg;
static int lines; // data lines left before the heading is reprinted

//
// Whole percent of `part' in `total'.  iostat's pcent() at a hundredth of the resolution --
// halve both sides until the multiply fits, rather than dividing first and losing the digit
// every time.  An int is 41 bits here, so part * 100 must stay under 2^40.
//
static int pct(int part, int total)
{
    if (total <= 0)
        return 0;
    while (total > 1000000000) {
        part /= 2;
        total /= 2;
    }
    return (part * 100 + total / 2) / total;
}

//
// Whole units per second: n events over `ticks' ticks at HZ.
//
static int persec(int n, int ticks)
{
    if (ticks <= 0)
        return 0;
    while (n > 1000000000) {
        n /= 2;
        ticks /= 2;
    }
    return (n * HZ + ticks / 2) / ticks;
}

//
// One CPU category: the eight slots that share a CPU state, whatever the devices were doing.
//
static int cpusum(int base)
{
    int i, n = 0;

    for (i = 0; i < NCPUST; i++)
        n += delta[base + i];
    return n;
}

//
// A table, with one diagnostic for the whole program.  A short answer means the kernel and
// this program disagree about a size, which is worth refusing rather than half printing.
//
static int table(const char *name, void *buf, int len)
{
    if (kctl(name, KCTL_GET, buf, len) != len) {
        fprintf(stderr, "vmstat: cannot read %s\n", name);
        return -1;
    }
    return 0;
}

//
// The twelve rate counters.  A kernel exporting none of them is a kernel this program has
// nothing to say about.
//
static int rates(void)
{
    int i;

    for (i = 0; i < NRATE; i++)
        if (table(rname[i], &rcur[i], (int)sizeof rcur[i]) < 0)
            return -1;
    return 0;
}

//
// The proc-table scan.  RetroBSD's kernel keeps these four in `struct vmtotal' and
// recomputes them every five seconds; they are computed here instead, from a table ps and
// pstat already read, and are therefore current rather than up to five seconds old.
//
//   r  runnable and in core                     -- what the scheduler can dispatch now
//   b  asleep at or below PZERO                 -- sleep()'s own test for "uninterruptible",
//                                                  which is to say blocked on a resource
//   w  runnable and swapped out                 -- literally sched()'s swap-in search
//   avm the swappable images of everything live, in words
//
// SSYS is excluded throughout: proc[0] is the swapper, it is permanently SRUN, and counting
// it would put a floor of 1 under `r' on an idle machine.  p_pri is a char and char is
// unsigned on this target, so the test is `<= PZERO' and never `< 0' -- setpri() clamps to
// 0..127 (kernel/slp.c) and nothing here may assume otherwise.
//
static void scanproc(int *rp, int *bp, int *wp, int *avmp)
{
    register struct proc *p;
    int r = 0, b = 0, w = 0, avm = 0;

    for (p = xproc; p < &xproc[NPROC]; p++) {
        if (p->p_stat == 0 || (p->p_flag & SSYS))
            continue;
        if (p->p_stat != SZOMB)
            avm += p->p_size;
        if (p->p_stat == SRUN) {
            if (p->p_flag & SLOAD)
                r++;
            else
                w++;
        } else if (p->p_stat == SSLEEP && p->p_pri <= PZERO)
            b++;
    }
    *rp   = r;
    *bp   = b;
    *wp   = w;
    *avmp = avm;
}

//
// Free core, in words: the sum of coremap's extents.  pstat -s's prmap() sums the same
// array the same way and prints the fragments as well.
//
static int freecore(void)
{
    int i, n = 0;

    for (i = 0; i < CMAPSIZ; i++)
        n += xcore[i].m_size;
    return n;
}

//
// The words of shared text in core, for -p's `txt': the percentage of `avm' that is text
// somebody else may also be using.
//
static int textwords(void)
{
    int i, n = 0;

    for (i = 0; i < NTEXT; i++)
        if (xtext[i].x_count != 0)
            n += xtext[i].x_size;
    return n;
}

//
// The two headings.  Reprinted every nineteen data lines, as BSD does -- but on a SIGCONT
// here, because this kernel has no such signal.
//
static void printhdr(void)
{
    if (pflg) {
        printf(" procs  ");
        printf("      memory       ");
        printf("  swap  ");
        printf(" disks  ");
        printf("       faults       ");
        printf("      cpu\n");
        printf(" r  b  w");
        printf("     avm txt    fre");
        printf("   i   o");
        printf("  md  mb");
        printf("   in   sy   tr   cs");
        printf("  us  ni  sy  id\n");
    } else {
        printf(" procs  ");
        printf("     memory      ");
        printf(" disks  ");
        printf("    faults     ");
        printf("    cpu\n");
        printf(" r  b  w");
        printf("      avm     fre");
        printf("  md  mb");
        printf("   in   sy   cs");
        printf("  us  sy  id\n");
    }
    lines = 19;
}

//
// -s.  The counters as they stand, no interval and no proc scan.  The disk rows come last
// because they are the only pair that is per-device, and iostat is where they are a rate.
//
static int dosum(void)
{
    int i, wds[NDK];

    if (rates() < 0)
        return 1;
    if (table("dk_numb", numb, (int)sizeof numb) < 0 || table("dk_wds", wds, (int)sizeof wds) < 0)
        return 1;

    for (i = 0; i < NRATE; i++)
        printf("%9d %s\n", rcur[i], rlabel[i]);
    printf("%9d disk transfers\n", numb[DK_MD]);
    printf("%9d disk words moved\n", wds[DK_MD]);
    printf("%9d drum transfers\n", numb[DK_MB]);
    printf("%9d drum words moved\n", wds[DK_MB]);
    return 0;
}

//
// One periodic report.  Returns -1 if the kernel would not answer.
//
static int report(void)
{
    int i, ticks, r, b, w, avm, fre, txt;

    if (table("dk_time", cur, (int)sizeof cur) < 0 ||
        table("dk_numb", numb, (int)sizeof numb) < 0 || rates() < 0 ||
        table("proc", xproc, (int)sizeof xproc) < 0 ||
        table("coremap", xcore, (int)sizeof xcore) < 0)
        return -1;
    if (pflg && table("text", xtext, (int)sizeof xtext) < 0)
        return -1;

    // The interval, and the shadow that makes the next one an interval too.  The first pass
    // finds the shadow zeroed, so it reports everything since boot -- which is iostat's
    // arrangement and v7's before it.
    ticks = 0;
    for (i = 0; i < NDKTIME; i++) {
        delta[i] = cur[i] - prev[i];
        prev[i]  = cur[i];
        ticks += delta[i];
    }
    for (i = 0; i < NDK; i++) {
        dnumb[i] = numb[i] - pnumb[i];
        pnumb[i] = numb[i];
    }
    for (i = 0; i < NRATE; i++) {
        rdelta[i] = rcur[i] - rprev[i];
        rprev[i]  = rcur[i];
    }

    scanproc(&r, &b, &w, &avm);
    fre = freecore();

    if (--lines <= 0)
        printhdr();

    printf("%2d%3d%3d", r, b, w);
    if (pflg) {
        txt = textwords();
        printf("%8d%4d%7d", avm, avm ? txt * 100 / avm : 0, fre);
        printf("%4d%4d", persec(rdelta[R_SWPIN], ticks), persec(rdelta[R_SWPOUT], ticks));
    } else
        printf("%9d%8d", avm, fre);

    for (i = 0; i < NDK; i++)
        printf("%4d", persec(dnumb[i], ticks));

    printf("%5d%5d", persec(rdelta[R_INTR], ticks), persec(rdelta[R_SYSCALL], ticks));
    if (pflg)
        printf("%5d", persec(rdelta[R_TRAP], ticks));
    printf("%5d", persec(rdelta[R_SWTCH], ticks));

    // us and ni are one column unless -p asked for both, which is BSD's rule and the reason
    // the plain form has three CPU columns and not four.
    if (pflg) {
        printf("%4d%4d", pct(cpusum(O_USER), ticks), pct(cpusum(O_NICE), ticks));
    } else
        printf("%4d", pct(cpusum(O_USER) + cpusum(O_NICE), ticks));
    printf("%4d%4d\n", pct(cpusum(O_SYS), ticks), pct(cpusum(O_IDLE), ticks));

    fflush(stdout);
    return 0;
}

int main(int argc, char *argv[])
{
    int iter = 0;

    while (argc > 1 && argv[1][0] == '-') {
        if (argv[1][1] == 's' && argv[1][2] == 0)
            return dosum();
        else if (argv[1][1] == 'p' && argv[1][2] == 0)
            pflg++;
        else {
            fprintf(stderr, "usage: vmstat [ -p ] [ interval [ count ] ]\n");
            fprintf(stderr, "       vmstat -s\n");
            return 1;
        }
        argc--;
        argv++;
    }
    if (argc > 2)
        iter = atoi(argv[2]);

    for (;;) {
        if (report() < 0)
            return 1;

        // iostat's loop, arithmetic included: a count of 0 (none given) decrements to -1
        // and repeats for ever, and an interval is what there has to be to repeat at all.
        if (--iter == 0 || argc <= 1)
            break;
        sleep(atoi(argv[1]));
    }
    return 0;
}
