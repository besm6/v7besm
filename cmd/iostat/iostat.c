/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 2007 Robert Nordier. All rights reserved. */

//
// iostat -- report I/O statistics.
//
//      iostat [ -t ] [ -i ] [ interval [ count ] ]
//
// Task C8's third.  Five KCTL_GETs -- dk_time[], dk_numb[], dk_wds[], tk_nin, tk_nout -- and
// no device and no privilege, which is the whole of what the kctl(2) half of this costs
// (<sys/kctl.h>).
//
// dk_time IS A HISTOGRAM AND NOT FOUR COUNTERS, and every sum in this file follows from that.
// kernel/clock.c stamps dk_time[(dk_busy & 07) + o], with o being 0 for user, 8 for nice, 16
// for system and 24 for idle -- so the array is four CPU states crossed with eight I/O
// states, and BOTH halves of the subscript move: kernel/dev/md.c and kernel/dev/mb.c keep
// dk_busy, a bit each (DK_MD and DK_MB in <sys/param.h>).  So:
//
//   A CPU CATEGORY IS THE SUM OF EIGHT SLOTS -- cpusum() below.  Reading slot 0, 8, 16 and 24
//   alone, which is what this program did while dk_busy was stuck at 0, drops every tick
//   taken while a disk or a drum had an exchange outstanding.  That is silent, and it reads
//   as an idle machine rather than as a missing number.
//
//   A DEVICE'S BUSY SHARE IS THE SUM OVER ALL FOUR CPU GROUPS of the slots with its bit up --
//   busysum().  That is the %bsy column, and it is a measurement of the drive rather than of
//   the CPU, which is why it cuts the histogram the other way.
//
// ONE SLOT FOR ALL 64 DISK UNITS.  dk_busy is three bits wide because it is the low three of
// clock.c's subscript, and dev/md.c addresses MDNUNIT = 64 drives; MD is therefore the disks
// TOGETHER and MB the drums together.  A per-drive breakdown needs a wider histogram than
// dk_time, and nothing has asked for one.  There is no cp_time on this system: dk_time is it.
//
// THE ARITHMETIC IS INTEGER, and that is the substantive port decision.  v7's iostat is the
// most float-dependent program of its size in the tree -- two `double' arrays and thirteen
// %6.2f -- and none of it survives:
//
//   b6_prog() cannot link -lm (scripts/BesmCross.cmake has no LIBS keyword; the gap is named
//   in lib/test/CMakeLists.txt), this machine has no IEEE 754 (../../lib/libm/README.md on
//   what its overflow does), and a percentage wants two decimal digits, not 53 bits of
//   mantissa.  So every quantity here is scaled: pcent() returns HUNDREDTHS OF A PERCENT and
//   prpc() prints one as `%3d.%02d', six characters, which is what %6.2f printed.  rate()
//   returns TENTHS of a unit per second and prrate() prints `%4d.%d', which is %6.1f.
//   The columns are identical to v7's, digit for digit.
//
//   THE WORDS COLUMN IS THE ONE EXCEPTION and is a plain %6d.  A drum moving 1,024 words an
//   exchange overruns prrate()'s four digits before the field is doing any work, and a tenth
//   of a word per second is not a quantity.  persec() is rate() without the scaling.
//
//   pcent() HALVES BOTH SIDES until the multiply fits.  An int here is 41 bits, so
//   part * 10000 must stay under 2^40; at HZ = 250 that is about eleven hours of uptime
//   before the scaling starts, and it costs a bit of the last decimal place when it does.
//   The alternative -- doing the divide first -- costs the decimal place ALWAYS.
//
// FOUR FLAGS ARE GONE, and none of them silently:
//
//   -s (documented) printed the 32 raw slots.  Twenty-eight of them are permanently zero
//   here; the flag would be a page of noise around four numbers -i already names.
//   -b (documented) printed buffer-cache statistics out of an `io_info' structure that this
//   kernel does not have and kernel/kctl.c exports nothing for.  Adding a row for it would
//   break the table's own rule: a row names the program that asked, and nothing asked.
//   -d and -a were undocumented in v7's own page.  -d stamped each report with ctime(); -a
//   printed the elapsed minutes.  Both are one line of shell away.
//
// ONE v7 BUG IS FIXED RATHER THAN CARRIED.  Its shadow loop is `for(i=0; i<40; i++)' over a
// `long etime[32]' -- eight elements past the end, into the numb[], wds[], tin and tout
// members that follow it in the same structure, read AND written back.  The loop below runs
// to NDKTIME.
//
// WHAT STAYS EXACTLY AS IT WAS: the first report is cumulative since boot and every later
// one covers the interval alone, which is what the shadow array is for; and a bare `iostat'
// with no interval prints one report and exits, while an interval with no count repeats for
// ever.  Both of those are v7's, down to the arithmetic of the loop.
//
// NOT SETUID, and in /etc rather than /bin only because v7's page is section 1M.
//
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/kctl.h>
#include <sys/param.h>

// The base of each CPU group in dk_time[], in the order v7's header names them.  NDKTIME,
// NDK, DK_MD and DK_MB are <sys/param.h>'s -- shared with the kernel that fills the array.
#define O_USER 0
#define O_NICE 8
#define O_SYS  16
#define O_IDLE 24
#define NCPUST 8 // I/O states to a CPU group: the width of dk_busy's field

static int cur[NDKTIME], prev[NDKTIME], delta[NDKTIME];
static int numb[NDK], pnumb[NDK], dnumb[NDK];
static int wds[NDK], pwds[NDK], dwds[NDK];
static int tin, tout, ptin, ptout, dtin, dtout;

static int tflg, iflg;

//
// Hundredths of a percent of `part' in `total'.  See the header: both sides are halved
// until the multiply fits in an int, rather than dividing first and losing the decimals.
//
static int pcent(int part, int total)
{
    if (total <= 0)
        return 0;
    while (total > 10000000) {
        part /= 2;
        total /= 2;
    }
    return (part * 10000 + total / 2) / total;
}

static void prpc(int hundredths)
{
    printf("%3d.%02d", hundredths / 100, hundredths % 100);
}

//
// Tenths of a character per second: n characters over `ticks' ticks at HZ.
//
static int rate(int n, int ticks)
{
    if (ticks <= 0)
        return 0;
    while (n > 100000000) {
        n /= 2;
        ticks /= 2;
    }
    return (n * HZ * 10 + ticks / 2) / ticks;
}

static void prrate(int tenths)
{
    printf("%4d.%d", tenths / 10, tenths % 10);
}

//
// Whole units per second -- rate() without the tenths, for a column whose quantity is too
// big for four digits and a decimal point.
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
// The device headings.  A conditional rather than an array of char *: a two-entry table
// would have to be initialized from string literals, and this compiler's rules about where
// a literal may initialize a pointer are worth not testing for two words (../README.md).
//
static const char *dkname(int dk)
{
    return dk == DK_MD ? "MD" : "MB";
}

//
// One device's busy time: the slots with its bit up, summed across all four CPU groups.  The
// subscript's low three bits ARE dk_busy, so `i & 07' recovers which devices were busy in
// the tick that landed in slot i.
//
static int busysum(int dk)
{
    int i, n = 0;

    for (i = 0; i < NDKTIME; i++)
        if ((i & 07) & (1 << dk))
            n += delta[i];
    return n;
}

//
// Fetch the five variables.  A kernel that exports none of them is a kernel this program has
// nothing to say about, so say so once and stop.
//
static int fetch(void)
{
    if (kctl("dk_time", KCTL_GET, cur, sizeof cur) != (int)sizeof cur ||
        kctl("dk_numb", KCTL_GET, numb, sizeof numb) != (int)sizeof numb ||
        kctl("dk_wds", KCTL_GET, wds, sizeof wds) != (int)sizeof wds ||
        kctl("tk_nin", KCTL_GET, &tin, sizeof tin) != (int)sizeof tin ||
        kctl("tk_nout", KCTL_GET, &tout, sizeof tout) != (int)sizeof tout) {
        fputs("iostat: this kernel exports no I/O statistics\n", stderr);
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    int i, iter = 0, total;

    while (argc > 1 && argv[1][0] == '-') {
        if (argv[1][1] == 't')
            tflg++;
        else if (argv[1][1] == 'i')
            iflg++;
        else {
            fprintf(stderr, "iostat: unknown option %s\n", argv[1]);
            return 1;
        }
        argc--;
        argv++;
    }
    if (argc > 2)
        iter = atoi(argv[2]);

    // The two headings.  A device gets 18 characters -- three columns of six -- and the
    // CPU block stays LAST, which is not cosmetic: test/run-iostat-test.sh cuts the four
    // percentages off the end of the line, and putting a variable-width block after them
    // would make that arithmetic depend on NDK.
    if (!iflg) {
        if (tflg)
            printf("         TTY");
        for (i = 0; i < NDK; i++)
            printf("                %s", dkname(i));
        printf("        PERCENT\n");
        if (tflg)
            printf("   tin  tout");
        for (i = 0; i < NDK; i++)
            printf("  %%bsy   tps   wps");
        printf("  user  nice systm  idle\n");
    }

    for (;;) {
        if (fetch() < 0)
            return 1;

        // The interval, and the shadow that makes the next one an interval too.  The first
        // pass finds the shadow zeroed, so it reports everything since boot.
        for (i = 0; i < NDKTIME; i++) {
            delta[i] = cur[i] - prev[i];
            prev[i]  = cur[i];
        }
        for (i = 0; i < NDK; i++) {
            dnumb[i] = numb[i] - pnumb[i];
            pnumb[i] = numb[i];
            dwds[i]  = wds[i] - pwds[i];
            pwds[i]  = wds[i];
        }
        dtin  = tin - ptin;
        dtout = tout - ptout;
        ptin  = tin;
        ptout = tout;

        total = 0;
        for (i = 0; i < NDKTIME; i++)
            total += delta[i];

        // -i is left as it was: four CPU lines and no devices.  It is the flag for a script
        // that wants one number, and a script asking for `idle' does not want six more.
        if (iflg) {
            prpc(pcent(cpusum(O_IDLE), total));
            printf(" idle\n");
            prpc(pcent(cpusum(O_USER), total));
            printf(" user\n");
            prpc(pcent(cpusum(O_NICE), total));
            printf(" nice\n");
            prpc(pcent(cpusum(O_SYS), total));
            printf(" system\n");
        } else {
            if (tflg) {
                prrate(rate(dtin, total));
                prrate(rate(dtout, total));
            }
            for (i = 0; i < NDK; i++) {
                prpc(pcent(busysum(i), total));
                prrate(rate(dnumb[i], total));
                printf("%6d", persec(dwds[i], total));
            }
            prpc(pcent(cpusum(O_USER), total));
            prpc(pcent(cpusum(O_NICE), total));
            prpc(pcent(cpusum(O_SYS), total));
            prpc(pcent(cpusum(O_IDLE), total));
            printf("\n");
        }

        // v7's loop, arithmetic included: a count of 0 (none given) decrements to -1 and
        // repeats for ever, and an interval is what there has to be to repeat at all.
        if (--iter == 0 || argc <= 1)
            break;
        sleep(atoi(argv[1]));
    }
    return 0;
}
