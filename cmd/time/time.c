/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// time -- run a command and report how long it took.
//
//      time command [ argument ... ]
//
// One of task C2b's five (../README.md), and the one with real work in it.  It is also the
// first program on this image that forks, execs and then MEASURES: everything before it
// either did its own job or, in rm -r and mv, execed a helper and only cared whether it
// succeeded.
//
// THE INTERFACE NEEDED NOTHING.  times(2) is a real system call here -- sysent[43],
// kernel/sys4.c copying four consecutive time_t words out of the u-area -- and
// <sys/times.h> already declares the C11 `struct tms' whose member names v7's source
// happens to use.  What did need work is the arithmetic on top of it.
//
// 1.  HZ IS 250, NOT 60, and this program is built out of that number twice over.  v7's
//     `printt("real", (after-before) * 60)' converts seconds to PDP-11 line-clock ticks,
//     and its radix table
//
//         char quant[] = { 6, 10, 10, 6, 10, 6, 10, 10, 10 };
//
//     decodes them back: the leading 6 turns 60ths into TENTHS of a second and the rest is
//     10/10 for seconds, 6/10 for minutes and 10/10/10 for hours.  On this machine a tick is
//     1/250 s (HZ in <sys/param.h>, CLOCKS_PER_SEC in <time.h>, and lib/libc/man/times.2
//     states it outright), so the multiplier is CLOCKS_PER_SEC and the leading radix is
//     CLOCKS_PER_SEC/10.  Nothing else in the table moves, and the printed format is
//     unchanged: hours:minutes:seconds.tenths, with the sub-tenth digit computed and then
//     discarded exactly as v7 discarded its 60ths.
//
// 2.  printt() WROTE NUL BYTES INTO ITS OUTPUT, and that is an upstream bug fixed here
//     rather than carried.  sep[] and nsep[] hold '\0' to mean `no separator between these
//     two digits', and v7 fprintf'd the '\0' anyway -- invisible on a terminal, which is the
//     only place v7 ever looked at it, but a real NUL in a file the moment anyone writes
//     `time cmd 2>log'.  That is precisely how kernel/test/utils captures it.  The separator
//     is now emitted only when there is one.  Nothing about the visible layout changes.
//
// 3.  sys_errlist[errno] -> strerror(errno).  The array exists (lib/libc/gen/errlst.c) but
//     is declared in no header, so v7's `extern char *sys_errlist[]' was the only way to
//     reach it and the subscript was unchecked.  strerror() is bounds-checked and is what
//     the rest of this image's diagnostics already print.  `extern int errno' had to go in
//     any case: <errno.h> defines errno as a macro, as C11 requires.
//
// 4.  execvp() IS DECLARED IN <unistd.h> NOW.  v7 put it in no header, so every caller in
//     this tree opened with a declaration of its own; this port added it and execlp()
//     beside the four exec forms already there, and lib/test/execs.c dropped its copies.
//     Its default path, ":/bin:/usr/bin" (lib/libc/gen/execvp.c), is what lets `time sleep 1'
//     resolve on an image whose shell exports no PATH.
//
// The rest is the mechanical C11 pass: `long a' is a one-word time_t here (§3), `register p'
// and `register i' were untyped, printt() was called before it was defined, and the four
// tables were unqualified globals.
//
// WHAT IS NOT CHANGED, deliberately: `while (wait(&status) != p) times(&obuffer);' re-reads
// the child times on every unrelated child reaped, which is v7's way of not counting them;
// and the report goes to the DIAGNOSTIC output, as v7 put it -- time.1 says so, and it is
// why kernel/test/utils.sh redirects this program's stderr and nothing else's.
//
// NOT SETUID: fork, exec, wait and times all act on the caller's own process and children.
//
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

//
// The radix of each printed position, least significant first.  quant[0] is the number of
// ticks in a tenth of a second and its digit is never printed -- it exists only to divide
// the tick count down to tenths.  Then tenths, seconds, tens of seconds, minutes, tens of
// minutes, and three digits of hours.
//
_Static_assert(CLOCKS_PER_SEC % 10 == 0, "a tick count must divide into whole tenths");
static const int quant[9] = { CLOCKS_PER_SEC / 10, 10, 10, 6, 10, 6, 10, 10, 10 };

// What to print in place of a leading zero, and the separator that follows each position.
// A '\0' means `nothing here'.  sep[] is used once a non-zero digit has been seen and nsep[]
// before that, so that the colons of an all-zero hours field come out as spaces.
static const char pad[]  = "000      ";
static const char sep[]  = "\0\0.\0:\0:\0\0";
static const char nsep[] = "\0\0.\0 \0 \0\0";

static void printt(const char *s, time_t a);

int main(int argc, char *argv[])
{
    struct tms buffer, obuffer;
    int status;
    pid_t p;
    time_t before, after;

    if (argc <= 1)
        return 0;
    time(&before);
    p = fork();
    if (p == -1) {
        fprintf(stderr, "Try again.\n");
        return 1;
    }
    if (p == 0) {
        execvp(argv[1], &argv[1]);
        fprintf(stderr, "%s: %s\n", argv[1], strerror(errno));
        return 1;
    }
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    times(&obuffer);
    while (wait(&status) != p)
        times(&obuffer);
    time(&after);
    if ((status & 0377) != 0)
        fprintf(stderr, "Command terminated abnormally.\n");
    times(&buffer);
    fprintf(stderr, "\n");
    printt("real", (after - before) * CLOCKS_PER_SEC);
    printt("user", buffer.tms_cutime - obuffer.tms_cutime);
    printt("sys ", buffer.tms_cstime - obuffer.tms_cstime);
    return status >> 8;
}

//
// Print one labelled interval, `a' in ticks.  The digits are peeled off least significant
// first and then printed most significant first, leading zeros replaced out of pad[].
//
static void printt(const char *s, time_t a)
{
    int digit[9];
    int i;
    int nonzero;
    char c;

    for (i = 0; i < 9; i++) {
        digit[i] = a % quant[i];
        a /= quant[i];
    }
    fputs(s, stderr);
    nonzero = 0;
    while (--i > 0) {
        c = digit[i] != 0 ? digit[i] + '0' : nonzero ? '0' : pad[i];
        putc(c, stderr);
        nonzero |= digit[i];
        c = nonzero ? sep[i] : nsep[i];
        if (c != '\0') // v7 printed the NUL too; see the header
            putc(c, stderr);
    }
    putc('\n', stderr);
}
