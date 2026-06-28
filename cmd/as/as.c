//
// Assembler for BESM-6.
// Driver: global state, command line and pass sequencing.
//
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "as.h"

FILE *sfile[SABS], *rfile[SABS];
long count[SABS];
short segm;
char *infile, *outfile = "a.out";
char tfilename[] = "/tmp/asXXXXXX";
short line;  // current line number
short debug; // debug flag
short xflags, Xflag, uflag;
short stlength; // symbol table length in bytes
short stalign;  // symbol table alignment
long cbase, tbase, dbase, adbase, bbase;
struct nlist stab[STSIZE];
short stabfree;
char space[SPACESZ]; // storage for symbol names
short lastfree;      // counter of used space
short regleft;       // register number to the left of the instruction
struct constent constab[CSIZE];
short nconst;
char name[256];
struct word intval;
short extref;
short blexflag, backlex, blextype;
short hashtab[HASHSZ], hashctab[HCMDSZ];
short hashconst[HCONSZ];
short aflag; // don't align on word boundary

// Fatal error message.
noreturn void uerror(char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fprintf(stderr, "as: ");
    if (infile)
        fprintf(stderr, "%s, ", infile);
    if (line)
        fprintf(stderr, "%d: ", line);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(1);
}

static void startup(void)
{
    register short i;

    int fd = mkstemp(tfilename);
    if (fd == -1) {
        uerror("cannot create temporary file %s", tfilename);
    } else {
        close(fd);
    }
    for (i = STEXT; i < SBSS; i++) {
        if (!(sfile[i] = fopen(tfilename, "w+")))
            uerror("cannot open %s", tfilename);
        unlink(tfilename);
        if (!(rfile[i] = fopen(tfilename, "w+")))
            uerror("cannot open %s", tfilename);
        unlink(tfilename);
    }
    line = 1;
}

static void usage(void)
{
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "    mkb-as [-uaxXd] [-o outfile] [infile]\n");
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
    register short i;
    register char *cp;
    int ofile = 0;

    // parse flags

    for (i = 1; i < argc; i++) {
        switch (argv[i][0]) {
        case '-':
            for (cp = argv[i] + 1; *cp; cp++) {
                switch (*cp) {
                case 'd': // debug flag
                    debug++;
                    break;
                case 'X':
                    Xflag++;
                case 'x':
                    xflags++;
                    break;
                case 'a': // don't align on word boundary
                    aflag++;
                    break;
                case 'u':
                    uflag++;
                    break;
                case 'o': // output file
                    if (ofile)
                        uerror("too many -o flags");
                    ofile = 1;
                    if (cp[1]) {
                        // -ofile
                        outfile = cp + 1;
                        while (*++cp)
                            ;
                        --cp;
                    } else if (i + 1 < argc)
                        // -o file
                        outfile = argv[++i];
                    break;
                default:
                    fprintf(stderr, "Unknown option: %s\n", cp);
                    usage();
                }
            }
            break;
        default:
            if (infile)
                uerror("too many input files");
            infile = argv[i];
            break;
        }
    }
    if (!infile && isatty(0))
        usage();

    // set up input/output

    if (infile && !freopen(infile, "r", stdin))
        uerror("cannot open %s", infile);
    if (!freopen(outfile, "w", stdout))
        uerror("cannot open %s", outfile);

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
