/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// cat -- concatenate files on the standard output.
//
// The v7 program, unchanged in what it does.  The C11 pass is the usual one (b6parse has no
// implicit `int', no K&R parameter lists and no untyped `register c;'), and there is one
// change of substance, below.
//
// THE UNINITIALIZED `dev'.  v7 wrote `int dev, ino = -1;' and then filled dev in only when
// the standard output is neither a character nor a block device:
//
//      fstat(fileno(stdout), &statb);
//      statb.st_mode &= S_IFMT;
//      if (statb.st_mode != S_IFCHR && statb.st_mode != S_IFBLK) { dev = ...; ino = ...; }
//
// so on a terminal -- which on this system is the NORMAL case, since /etc/init hands the
// shell three descriptors on /dev/console -- `dev' is read at `statb.st_dev == dev' having
// never been written.  It happened to be harmless because ino stayed -1 and && short
// circuits, but it is undefined either way and one edit from being a real misdetection.
// Both are initialized to -1 now: that is NODEV (sys/param.h), so no real st_dev can equal
// it and the "input is output" test keeps behaving exactly as v7 intended.
//
// The flag loop's `fflg || ((*++argv)[0] == '-' && (*argv)[1] == '\0')' has been
// parenthesized, not rearranged.  The short circuit is load-bearing: when fflg is set argc
// was forced to 2 and there is no real argument, so *++argv must NOT be evaluated and argv
// must NOT advance.  Do not hoist the increment out.
//
// Nothing here is BESM-6-shaped.  ++argv walks a char ** -- a thin word pointer, since it
// addresses a word-sized object -- while (*argv)[1] indexes a fat char *, and the compiler
// handles both (doc/Besm6_Data_Representation.md section 7).  The one number that changed is
// stdbuf's: BUFSIZ is BSIZE here, 3072 bytes, so the buffer is 512 words rather than the 512
// BYTES of the PDP-11.
//
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// The standard output's buffer, given to setbuf() below.  It must be exactly BUFSIZ
// characters -- that is the contract lib/libc/stdio/setbuf.c documents -- which here is one
// filesystem block: 3072 chars, 512 words, the largest object in the program.
static char stdbuf[BUFSIZ];

int main(int argc, char **argv)
{
    int fflg = 0; // set when cat is reading the standard input rather than named files
    FILE *fi;
    int c;
    int dev = -1, ino = -1; // NODEV/no inode: see the header
    struct stat statb;

    setbuf(stdout, stdbuf);

    // The options.  v7 accepts exactly one, -u; a bare `-' means the standard input and
    // stops the scan, and anything else is skipped in silence.
    for (; argc > 1 && argv[1][0] == '-'; argc--, argv++) {
        switch (argv[1][1]) {
        case 0:
            break;
        case 'u':
            setbuf(stdout, (char *)NULL);
            continue;
        }
        break;
    }

    // Note what the standard output IS, so that `cat x >>x' can be refused below.  A device
    // is exempt: two descriptors on the same terminal are not a loop, they are the ordinary
    // case, and comparing them would refuse every `cat file' typed at the console.
    fstat(fileno(stdout), &statb);
    statb.st_mode &= S_IFMT;
    if (statb.st_mode != S_IFCHR && statb.st_mode != S_IFBLK) {
        dev = statb.st_dev;
        ino = statb.st_ino;
    }

    // No file arguments: read the standard input.  Forcing argc to 2 makes the loop below
    // run exactly once, and fflg is what stops it dereferencing an argument that is not there.
    if (argc < 2) {
        argc = 2;
        fflg++;
    }

    while (--argc > 0) {
        if (fflg || ((*++argv)[0] == '-' && (*argv)[1] == '\0'))
            fi = stdin;
        else {
            if ((fi = fopen(*argv, "r")) == NULL) {
                fprintf(stderr, "cat: can't open %s\n", *argv);
                continue;
            }
        }

        // The same file for input and output would append to itself forever.
        fstat(fileno(fi), &statb);
        if (statb.st_dev == dev && statb.st_ino == ino) {
            fprintf(stderr, "cat: input %s is output\n", fflg ? "-" : *argv);
            fclose(fi);
            continue;
        }

        while ((c = getc(fi)) != EOF)
            putchar(c);
        if (fi != stdin)
            fclose(fi);
    }
    return 0;
}
