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

#include "besm6/b.out.h"

#include "size.h"

#define W 6 /* word length in bytes */

static int header; /* whether the header has already been printed */
static int wflag;  /* print sizes in words */


/* Print the command-line usage summary. */
static void usage(void)
{
    printf("Usage:\n");
    printf("    size [-w] file...\n");
    printf("Options:\n");
    printf("    -w          Print sizes in words instead of bytes\n");
}

static void size(const char *fname)
{
    struct exec buf;
    long sum;
    FILE *f;

    if ((f = fopen(fname, "r")) == NULL) {
        printf("size: %s not found\n", fname);
        return;
    }
    if (!fgethdr(f, &buf) || N_BADMAG(buf)) {
        printf("size: %s not an object file\n", fname);
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
                    fprintf(stderr, "size: bad flag %c\n",
                            **argv);
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
