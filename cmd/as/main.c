//
// Assembler for BESM-6.
// Command-line front end: parse arguments and invoke assemble().
//
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "as.h"

// Diagnostic prefix: the basename of argv[0] (set in main()), "as" by default.
static char *progname = "as";

//
// Print a fatal error message and exit, in GNU/Clang style:
//      progname: [file:line: ]error: message
// The location prefix (from format_location) is shown when known.  This is the
// only way the assembler reports an unrecoverable problem; it never returns.
// (The unit-test harness links its own version that records the error instead
// of exiting.)
//
noreturn void fatal(char *fmt, ...)
{
    va_list ap;

    char loc[SRCNAME_MAX + 32];

    va_start(ap, fmt);
    format_location(loc, sizeof loc);
    fprintf(stderr, "%s: %serror: ", progname, loc);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(1);
}

//
// Print the command-line usage summary and exit with an error.
//
static void usage(void)
{
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "    %s [-uaxXd] [-o outfile] [infile]\n", progname);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "    -o filename     Set output file name, default stdout\n");
    fprintf(stderr, "    -u              Treat undefined names as error\n");
    fprintf(stderr, "    -a              Don't align on word boundary\n");
    fprintf(stderr, "    -x              Discard local symbols\n");
    fprintf(stderr, "    -X              Discard locals starting with '.'\n");
    fprintf(stderr, "    -d              Debug mode\n");
    exit(1);
}

//
// Program entry point.  Parse the command-line options into an
// assembler_args, then hand off to assemble().  Each "-..." argument is a
// bundle of single-letter flags; anything else is the (single) input file.
//
int main(int argc, char *argv[])
{
    int i;
    char *cp;
    int ofile                  = 0;
    struct assembler_args args = {
        .outfile = "a.out",
    };

    // Derive the diagnostic prefix from argv[0]'s basename (fallback "as").
    if (argc > 0 && argv[0] && argv[0][0]) {
        char *slash = strrchr(argv[0], '/');
        progname    = slash ? slash + 1 : argv[0];
    }

    // parse flags

    for (i = 1; i < argc; i++) {
        switch (argv[i][0]) {
        case '-':
            // Walk the letters of one "-xyz" argument, setting a flag for each.
            for (cp = argv[i] + 1; *cp; cp++) {
                switch (*cp) {
                case 'd': // debug flag
                    args.debug++;
                    break;
                case 'X':
                    // -X implies -x: it discards a subset of the locals that -x
                    // discards, so fall through to also set xflags.
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
                case 'o': // output file name
                    if (ofile)
                        fatal("too many -o flags");
                    ofile = 1;
                    if (cp[1]) {
                        // "-ofile": the name is glued to the flag.  Take the
                        // rest of this argument and advance cp to its end.
                        args.outfile = cp + 1;
                        while (*++cp)
                            ;
                        --cp;
                    } else if (i + 1 < argc)
                        // "-o file": the name is the next argument.
                        args.outfile = argv[++i];
                    break;
                default:
                    fprintf(stderr, "%s: error: unknown option '%c'\n", progname, *cp);
                    usage();
                }
            }
            break;
        default:
            if (args.infile)
                fatal("too many input files");
            args.infile = argv[i];
            break;
        }
    }
    if (!args.infile && isatty(0))
        usage();

    return assemble(&args);
}
