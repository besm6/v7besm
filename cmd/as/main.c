//
// Assembler for BESM-6.
// Command-line front end: parse arguments and invoke assemble().
//
#include "as.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

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
    struct assembler_args args = {
        .outfile = "a.out",
    };

    // parse flags

    for (i = 1; i < argc; i++) {
        switch (argv[i][0]) {
        case '-':
            for (cp = argv[i] + 1; *cp; cp++) {
                switch (*cp) {
                case 'd': // debug flag
                    args.debug++;
                    break;
                case 'X':
                    args.Xflag++;
                case 'x':
                    args.xflags++;
                    break;
                case 'a': // don't align on word boundary
                    args.aflag++;
                    break;
                case 'u':
                    args.uflag++;
                    break;
                case 'o': // output file
                    if (ofile)
                        uerror("too many -o flags");
                    ofile = 1;
                    if (cp[1]) {
                        // -ofile
                        args.outfile = cp + 1;
                        while (*++cp)
                            ;
                        --cp;
                    } else if (i + 1 < argc)
                        // -o file
                        args.outfile = argv[++i];
                    break;
                default:
                    fprintf(stderr, "Unknown option: %s\n", cp);
                    usage();
                }
            }
            break;
        default:
            if (args.infile)
                uerror("too many input files");
            args.infile = argv[i];
            break;
        }
    }
    if (!args.infile && isatty(0))
        usage();

    return assemble(&args);
}
