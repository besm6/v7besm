/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// tail -- print the last part of a file.
//
//      tail [ +-number[lbcr] ] [ file ]
//
// One of task C5b's seven (../TODO.md).  No §2 in it -- every bound in the ring below is an
// int index -- and no stdio at all: tail is read(2) and write(2) end to end, which puts it in
// the company of tee, test and getty at the bottom of ../README.md §6's table.
//
// `int errno;' HAD TO GO, AND NOT BE RENAMED.  v7 includes <errno.h> and then defines the
// variable itself, which C11 reserves; but the definition is not merely redundant here, it is
// wrong, because the value tail reads has to be the one LIBC's lseek() set.  Deleting the
// line is the whole fix, and the probe it feeds is written properly besides -- v7 tests errno
// without looking at whether lseek failed, so any earlier errno of ESPIPE would have made
// tail treat a seekable file as a pipe.
//
// `-b' COUNTS BSIZE AND NOT 512 (§4), which ../TODO.md left to this task to settle.  512
// named a PDP-11 disk block and names nothing on this machine, so the rule dd(1) was ported
// under applies -- a constant is the user's business only while it still names something
// here.  `b' therefore means one filesystem block, 3072 bytes, exactly as dd's `b' suffix
// does, and tail.1 states the number, which it never did in v7.
//
// The three OTHER 512s in this file are not that unit and did not move: they are the size of
// a read(2), and nothing user-visible depends on them.  They are BSIZE now for throughput
// alone -- one filesystem block per call rather than a sixth of one.
//
// THE ARGUMENT SHUFFLE IS GONE.  With no `where' argument v7 wrote `arg = "-10l"; argc++;
// argv--;' so that a later argv[2] would name argv[1]; that is a pointer before the start of
// the argument array, and an explicit variable says the same thing without it.
//
// AND ONE UPSTREAM BUG: the usage message is 29 bytes and v7 wrote 30 of them, so the
// diagnostic carried whatever byte followed the string.
//
// WHAT IS LEFT ALONE: the ring below, verbatim.  LBIN is 4097 and not 4096 for one reason --
// `-r' forces a trailing newline into bin[i] when the input did not end with one, and the
// extra slot is where it goes.
//
// NOT SETUID: it opens what the caller could open itself.
//
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define LBIN 4097 // the ring, plus the one slot -r's forced newline needs

static struct stat statb;
static char bin[LBIN];

static int digit(int c);
static void usage(void);

int main(int argc, char **argv)
{
    int n, di;
    int i, j, k;
    int partial, piped, bylines, bkwds, fromend, lastnl;
    int p;
    char *arg;
    char *file;

    arg = argc > 1 ? argv[1] : (char *)0;
    if (!arg || (*arg != '-' && *arg != '+')) {
        file = arg;
        arg  = "-10l";
    } else
        file = argc > 2 ? argv[2] : (char *)0;

    fromend = *arg == '-';
    arg++;
    n = 0;
    while (digit(*arg))
        n = n * 10 + *arg++ - '0';
    if (!fromend && n > 0)
        n--;
    if (file) {
        close(0);
        if (open(file, 0) != 0) {
            write(2, "tail: can't open ", 17);
            write(2, file, strlen(file));
            write(2, "\n", 1);
            return 1;
        }
    }

    // Is descriptor 0 seekable?  Two changes from v7 and the first is the fix: v7 reads errno
    // without asking whether lseek failed, so any earlier ESPIPE made tail treat a seekable
    // file as a pipe.  The second is only the order -- this is after the redirect above, where
    // v7 probed before it, so `cat x | tail -5c file' no longer decides about the PIPE when it
    // is about to read the FILE.  That was never a wrong answer, only the slow path taken over
    // a descriptor that could have been seeked.
    piped = 0;
    if (lseek(0, (off_t)0, 1) == (off_t)-1 && errno == ESPIPE)
        piped = 1;

    bylines = 0;
    bkwds   = 0;
    switch (*arg) {
    case 'b':
        n *= BSIZE; // one FILESYSTEM block, not the PDP-11's 512 -- see the header
        break;
    case 'c':
        break;
    case 'r':
        if (n == 0)
            n = LBIN;
        bkwds   = 1;
        fromend = 1;
        bylines = 1;
        break;
    case '\0':
    case 'l':
        bylines = 1;
        break;
    default:
        usage();
        return 1;
    }
    if (fromend)
        goto keep;

    // Seek from the beginning.
    if (bylines) {
        j = 0;
        p = 0;
        while (n-- > 0) {
            do {
                if (j-- <= 0) {
                    p = 0;
                    j = read(0, bin, BSIZE);
                    if (j-- <= 0)
                        return 0;
                }
            } while (bin[p++] != '\n');
        }
        write(1, &bin[p], j);
    } else if (n > 0) {
        if (!piped)
            fstat(0, &statb);
        if (piped || (statb.st_mode & S_IFMT) == S_IFCHR)
            while (n > 0) {
                i = n > BSIZE ? BSIZE : n;
                i = read(0, bin, i);
                if (i <= 0)
                    return 0;
                n -= i;
            }
        else
            lseek(0, (off_t)n, 0);
    }
copy:
    while ((i = read(0, bin, BSIZE)) > 0)
        write(1, bin, i);
    return 0;

    // Seek from the end.
keep:
    if (n <= 0)
        return 0;
    if (!piped) {
        fstat(0, &statb);
        di = !bylines ? n : LBIN - 1;
        if (statb.st_size > di)
            lseek(0, (off_t)-di, 2);
        if (!bylines)
            goto copy;
    }
    partial = 1;
    for (;;) {
        i = 0;
        do {
            j = read(0, &bin[i], LBIN - i);
            if (j <= 0)
                goto brka;
            i += j;
        } while (i < LBIN);
        partial = 0;
    }
brka:
    if (!bylines) {
        k = n <= i ? i - n : partial ? 0 : n >= LBIN ? i + 1 : i - n + LBIN;
        k--;
    } else {
        if (bkwds && bin[i == 0 ? LBIN - 1 : i - 1] != '\n') { // force trailing newline
            bin[i] = '\n';
            if (++i >= LBIN) {
                i       = 0;
                partial = 0;
            }
        }
        k = i;
        j = 0;
        do {
            lastnl = k;
            do {
                if (--k < 0) {
                    if (partial) {
                        if (bkwds)
                            write(1, bin, lastnl + 1);
                        goto brkb;
                    }
                    k = LBIN - 1;
                }
            } while (bin[k] != '\n' && k != i);
            if (bkwds && j > 0) {
                if (k < lastnl)
                    write(1, &bin[k + 1], lastnl - k);
                else {
                    write(1, &bin[k + 1], LBIN - k - 1);
                    write(1, bin, lastnl + 1);
                }
            }
        } while (j++ < n && k != i);
    brkb:
        if (bkwds)
            return 0;
        if (k == i)
            do {
                if (++k >= LBIN)
                    k = 0;
            } while (bin[k] != '\n' && k != i);
    }
    if (k < i)
        write(1, &bin[k + 1], i - k - 1);
    else {
        write(1, &bin[k + 1], LBIN - k - 1);
        write(1, bin, i);
    }
    return 0;
}

static int digit(int c)
{
    return c >= '0' && c <= '9';
}

// v7 wrote 30 bytes of a 29-byte string, so the diagnostic carried a stray byte.
static void usage(void)
{
    static const char msg[] = "usage: tail +_n[lbcr] [file]\n";

    write(2, msg, sizeof(msg) - 1);
}
