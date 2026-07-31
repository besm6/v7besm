/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// tee -- pipe fitting.  Copy standard input to standard output and to each named file.
//
//      tee [ -i ] [ -a ] [ file ... ]
//
// One of task C5a's six (../README.md), and the only one of them with no stdio at all: every
// byte goes through read(2) and write(2), which is why it is also the smallest.  It is the
// one file of the six that walks a buffer, and it does so with int indices rather than a
// cursor, so even this one carries none of §2.
//
// FOUR THINGS CHANGED, and the first three are this machine.
//
// 1.  THE BUFFERS WERE 512 BYTES.  That was the PDP-11's disk block, and 512 names nothing
//     here -- §4's rule, settled by dd: a constant is the user's business only while it
//     still names something on this machine.  in[] and out[] are BSIZE now, so a full
//     buffer is a filesystem block instead of a sixth of one, and the two loop bounds that
//     had to agree with them (the fill limit and the read size) come from the same name.
//     The cost is 1024 words of bss and it is the whole reason this program has any.
//
// 2.  openf[20] HAD NO BOUND.  v7 sized the table at its _NFILE and then let the argument
//     loop write past it -- into `n', `t' and `aflag', which are the next three objects --
//     so `tee a b c ... ' with twenty names corrupted the program's own state rather than
//     complaining.  The table is NOFILE (sys/param.h: the kernel's own per-process limit,
//     and the honest bound) and the loop stops at it with a diagnostic.  Nothing on this
//     machine could reach twenty open descriptors anyway; a program that scribbles on itself
//     when it does is still worth not shipping.
//
// 3.  ITS puts() COLLIDED WITH libc's.  v7's tee defines a two-line `puts' of its own that
//     writes to descriptor 2, appends no newline and returns nothing -- unrelated to C11's
//     in every particular, and the collision is silent, since the program's own definition
//     satisfies its own calls (../README.md §1: chmod's abs() and chown's isnumber() are the
//     precedent).  It is emsg() now, and it makes one write(2) per string rather than v7's
//     one per BYTE.
//
// 4.  `extern errno;' and `long lseek();' are gone in favour of the headers, which is §1.
//
// WHAT IS DELIBERATELY UNCHANGED IS THE 16-BYTE DRIBBLE.  When any output is a terminal or a
// pipe (`t'), stash() writes sixteen bytes at a time and the fill loop breaks early rather
// than waiting for a full buffer, so an interactive tee echoes a line as it is typed instead
// of a block at a time.  That is what makes tee usable in a pipeline and it is v7's; the
// number is arbitrary and stays as it is.
//
// NOT SETUID: it creates what the caller could create itself.
//
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int openf[NOFILE] = { 1 }; // output descriptors; [0] is standard output
static int n             = 1;     // how many of them are in use
static int t             = 0;     // any output is a terminal or a pipe: write in dribbles
static int aflag;

static char in[BSIZE];
static char out[BSIZE];

// A diagnostic, on the diagnostic output.  This is v7's `puts', renamed off libc's name and
// writing the whole string at once.
static void emsg(const char *s)
{
    write(2, s, strlen(s));
}

// Hand the first p bytes of out[] to every output.  d is the chunk: a whole buffer when
// every output is a file, sixteen bytes when one of them is a terminal or a pipe.
static void stash(int p)
{
    int k;
    int i;
    int d;

    d = t ? 16 : p;
    for (i = 0; i < p; i += d)
        for (k = 0; k < n; k++)
            write(openf[k], out + i, d < p - i ? d : p - i);
}

int main(int argc, char **argv)
{
    int r, w, p;
    struct stat buf;

    while (argc > 1 && argv[1][0] == '-') {
        switch (argv[1][1]) {
        case 'a':
            aflag++;
            break;
        case 'i':
        case 0:
            signal(SIGINT, SIG_IGN);
        }
        argv++;
        argc--;
    }
    fstat(1, &buf);
    t = (buf.st_mode & S_IFMT) == S_IFCHR;
    if (lseek(1, 0, 1) == -1 && errno == ESPIPE)
        t++;
    while (argc-- > 1) {
        // v7 wrote past the end of openf[] here, into the three ints that follow it.
        if (n >= NOFILE) {
            emsg("tee: too many output files\n");
            break;
        }
        if (aflag) {
            openf[n] = open(argv[1], O_WRONLY);
            if (openf[n] < 0)
                openf[n] = creat(argv[1], 0666);
            lseek(openf[n++], 0, 2);
        } else
            openf[n++] = creat(argv[1], 0666);
        if (stat(argv[1], &buf) >= 0) {
            if ((buf.st_mode & S_IFMT) == S_IFCHR)
                t++;
        } else {
            emsg("tee: cannot open ");
            emsg(argv[1]);
            emsg("\n");
            n--;
        }
        argv++;
    }
    r = w = 0;
    for (;;) {
        for (p = 0; p < BSIZE;) {
            if (r >= w) {
                if (t > 0 && p > 0)
                    break;
                w = read(0, in, BSIZE);
                r = 0;
                if (w <= 0) {
                    stash(p);
                    return 0;
                }
            }
            out[p++] = in[r++];
        }
        stash(p);
    }
}
