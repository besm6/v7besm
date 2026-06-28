//
// Assembler for BESM-6.
// Driver: global state, command line and pass sequencing.
//
#include "as.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct assembler as = {
    .outfile   = "a.out",
    .tfilename = "/tmp/asXXXXXX",
};

// Fatal error message.
noreturn void uerror(char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fprintf(stderr, "as: ");
    if (as.infile)
        fprintf(stderr, "%s, ", as.infile);
    if (as.line)
        fprintf(stderr, "%d: ", as.line);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(1);
}

static void startup(void)
{
    int i;

    int fd = mkstemp(as.tfilename);
    if (fd == -1) {
        uerror("cannot create temporary file %s", as.tfilename);
    } else {
        close(fd);
    }
    for (i = STEXT; i < SBSS; i++) {
        if (!(as.sfile[i] = fopen(as.tfilename, "w+")))
            uerror("cannot open %s", as.tfilename);
        unlink(as.tfilename);
        if (!(as.rfile[i] = fopen(as.tfilename, "w+")))
            uerror("cannot open %s", as.tfilename);
        unlink(as.tfilename);
    }
    as.line = 1;
}

static void usage(void)
{
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "    b6as [-uaxXd] [-o outfile] [infile]\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "    -o filename     Set output file name, default stdout\n");
    fprintf(stderr, "    -u              Treat undefined names as error\n");
    fprintf(stderr, "    -a              Don't align on word boundary\n");
    fprintf(stderr, "    -x              Discard local symbols\n");
    fprintf(stderr, "    -X              Discard locals starting with 'L' or '.'\n");
    fprintf(stderr, "    -d              Debug mode\n");
    exit(1);
}

int main(int argc, char *argv[])
{
    int i;
    char *cp;
    int ofile = 0;

    // parse flags

    for (i = 1; i < argc; i++) {
        switch (argv[i][0]) {
        case '-':
            for (cp = argv[i] + 1; *cp; cp++) {
                switch (*cp) {
                case 'd': // debug flag
                    as.debug++;
                    break;
                case 'X':
                    as.Xflag++;
                case 'x':
                    as.xflags++;
                    break;
                case 'a': // don't align on word boundary
                    as.aflag++;
                    break;
                case 'u':
                    as.uflag++;
                    break;
                case 'o': // output file
                    if (ofile)
                        uerror("too many -o flags");
                    ofile = 1;
                    if (cp[1]) {
                        // -ofile
                        as.outfile = cp + 1;
                        while (*++cp)
                            ;
                        --cp;
                    } else if (i + 1 < argc)
                        // -o file
                        as.outfile = argv[++i];
                    break;
                default:
                    fprintf(stderr, "Unknown option: %s\n", cp);
                    usage();
                }
            }
            break;
        default:
            if (as.infile)
                uerror("too many input files");
            as.infile = argv[i];
            break;
        }
    }
    if (!as.infile && isatty(0))
        usage();

    // set up input/output

    if (as.infile && !freopen(as.infile, "r", stdin))
        uerror("cannot open %s", as.infile);
    if (!freopen(as.outfile, "w", stdout))
        uerror("cannot open %s", as.outfile);

    i = getchar();
    ungetc(i == '#' ? ';' : i, stdin);

    startup();    // open temporary files
    hashinit();   // initialize hash tables
    pass1();      // first pass
    middle();     // intermediate actions
    makeheader(); // write the header
    pass2();      // second pass
    makereloc();  // write relocation files
    makesymtab(); // write the symbol table
    return 0;
}
