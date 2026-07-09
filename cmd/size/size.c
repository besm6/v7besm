/*
 *      DEMOS SVS-B operating system.
 *
 *      size [-w] file ...      - print the segment sizes of an object file.
 *                                If the "-w" flag is given, sizes are printed
 *                                in words, otherwise in bytes.
 *
 *      Author: S.V. Vakulenko (MIPT).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "besm6/b.out.h"

#include "size.h"

#define W 6 /* word length in bytes */

static int header;            /* whether the header has already been printed */
static int wflag;             /* print sizes in words */
static char *progname = "size"; /* diagnostic prefix: basename of argv[0] */


/* Print the command-line usage summary. */
static void usage(void)
{
    printf("Usage:\n");
    printf("    %s [-w] file...\n", progname);
    printf("Options:\n");
    printf("    -w          Print sizes in words instead of bytes\n");
}

static void size(const char *fname)
{
    struct exec buf;
    long sum;
    FILE *f;

    if ((f = fopen(fname, "r")) == NULL) {
        fprintf(stderr, "%s: error: %s not found\n", progname, fname);
        return;
    }
    if (!fgethdr(f, &buf) || N_BADMAG(buf)) {
        fprintf(stderr, "%s: error: %s not an object file\n", progname, fname);
        fclose(f);
        return;
    }
    if (header == 0) {
        printf("const\ttext\tdata\tbss\tdec\thex\n");
        header = 1;
    }
    sum = buf.a_const + buf.a_text + buf.a_data + buf.a_bss;
    if (wflag) {
        sum /= W;
        printf("%ld\t%ld\t%ld\t%ld\t%ld\t%lx\t%s\n", (long)buf.a_const / W,
               (long)buf.a_text / W, (long)buf.a_data / W, (long)buf.a_bss / W,
               sum, sum, fname);
    } else {
        printf("%ld\t%ld\t%ld\t%ld\t%ld\t%lx\t%s\n", (long)buf.a_const, (long)buf.a_text,
               (long)buf.a_data, (long)buf.a_bss, sum, sum, fname);
    }
    fclose(f);
}

int size_run(int argc, char **argv)
{
    int yesarg = 0; /* whether file-name arguments were given */

    /* Derive the diagnostic prefix from argv[0]'s basename (fallback "size"). */
    if (argc > 0 && argv[0] && argv[0][0]) {
        char *slash = strrchr(argv[0], '/');
        progname    = slash ? slash + 1 : argv[0];
    }

    /* Reset option state so repeated in-process runs start clean. */
    header = wflag = 0;

    while (--argc) {
        ++argv;
        if (**argv == '-') {
            while (*++*argv)
                switch (**argv) {
                case 'w':
                    wflag++;
                    break;
                default:
                    fprintf(stderr, "%s: error: bad flag %c\n",
                            progname, **argv);
                    return 1;
                }
            continue;
        }
        size(*argv);
        yesarg = 1;
    }
    if (!yesarg) {
        usage();
        return 1;
    }
    return 0;
}
