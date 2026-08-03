/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 2007 Robert Nordier. All rights reserved. */

//
// iostat -- report I/O statistics.
//
//      iostat [ -t ] [ -i ] [ interval [ count ] ]
//
// Task C8's third.  Three KCTL_GETs -- dk_time[], tk_nin, tk_nout -- and no device and no
// privilege, which is the whole of what the kctl(2) half of this task costs (<sys/kctl.h>).
//
// IT GETS A CPU COLUMN AND NO DISK COLUMN, and that is a fact about the kernel rather than
// about this program.  kernel/clock.c stamps dk_time[(dk_busy & 07) + o], with o being 0 for
// user, 8 for nice, 16 for system and 24 for idle -- so the array is a histogram of four
// CPU states crossed with eight I/O states.  dk_busy is written by NOTHING on this system
// (kernel/main.c defines it, no driver touches it), so the I/O half of every subscript is
// permanently 0 and only slots 0, 8, 16 and 24 ever move.  dk_busy, dk_numb[] and dk_wds[]
// are deliberately absent from kernel/ksym.c for exactly that reason -- an exported variable
// nobody updates makes a tool print zeros that read as measurements.  The HD/FD/CD columns
// come back with kernel/TODO.md task 38 and not before.  There is no cp_time here.
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
//   returns TENTHS of a character per second and prrate() prints `%4d.%d', which is %6.1f.
//   The columns are identical to v7's, digit for digit.
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
//   kernel does not have and kernel/ksym.c exports nothing for.  Adding a row for it would
//   break the table's own rule: a row names the program that asked, and nothing asked.
//   -d and -a were undocumented in v7's own page.  -d stamped each report with ctime(); -a
//   printed the elapsed minutes.  Both are one line of shell away.
//
// ONE v7 BUG IS FIXED RATHER THAN CARRIED.  Its shadow loop is `for(i=0; i<40; i++)' over a
// `long etime[32]' -- eight elements past the end, into the numb[], wds[], tin and tout
// members that follow it in the same structure, read AND written back.  The loop below runs
// to NDK.
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

#define NDK 32 // dk_time[] -- 4 CPU states x 8 I/O states

// The four subscripts kernel/clock.c actually reaches, in the order v7's header names them.
#define O_USER 0
#define O_NICE 8
#define O_SYS  16
#define O_IDLE 24

static int cur[NDK], prev[NDK], delta[NDK];
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
// Fetch the three variables.  A kernel that exports none of them is a kernel this program
// has nothing to say about, so say so once and stop.
//
static int fetch(void)
{
    if (kctl("dk_time", KCTL_GET, cur, sizeof cur) != (int)sizeof cur ||
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

    if (!iflg) {
        if (tflg)
            printf("         TTY");
        printf("        PERCENT\n");
        if (tflg)
            printf("   tin  tout");
        printf("  user  nice systm  idle\n");
    }

    for (;;) {
        if (fetch() < 0)
            return 1;

        // The interval, and the shadow that makes the next one an interval too.  The first
        // pass finds the shadow zeroed, so it reports everything since boot.
        for (i = 0; i < NDK; i++) {
            delta[i] = cur[i] - prev[i];
            prev[i]  = cur[i];
        }
        dtin  = tin - ptin;
        dtout = tout - ptout;
        ptin  = tin;
        ptout = tout;

        total = 0;
        for (i = 0; i < NDK; i++)
            total += delta[i];

        if (iflg) {
            prpc(pcent(delta[O_IDLE], total));
            printf(" idle\n");
            prpc(pcent(delta[O_USER], total));
            printf(" user\n");
            prpc(pcent(delta[O_NICE], total));
            printf(" nice\n");
            prpc(pcent(delta[O_SYS], total));
            printf(" system\n");
        } else {
            if (tflg) {
                prrate(rate(dtin, total));
                prrate(rate(dtout, total));
            }
            prpc(pcent(delta[O_USER], total));
            prpc(pcent(delta[O_NICE], total));
            prpc(pcent(delta[O_SYS], total));
            prpc(pcent(delta[O_IDLE], total));
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
