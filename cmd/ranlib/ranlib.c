/*
 *      DEMOS SVS-B operating system.
 *
 *      Build the symdef table for fast loading.
 *
 *      Author: S. Vakulenko.
 *      Version of 06.02.90.
 *
 *      Source code taken from UNIX 4.3 BSD.
 */

#include "symdef.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "besm6/ar.h"
#include "besm6/b.out.h"
#include "besm6/ranlib.h"

#include "archive.h" /* ar_run() -- the in-process archiver engine */

#define W 6 /* sizeof word of BESM-6 */

#define TABSZ    1000
#define STRTABSZ (TABSZ * 10)

static struct ar_hdr archdr;
static struct exec exh;

static long arsize;
static FILE *fi, *fo;
static long off, oldoff;
static int new;
static char firstname[31];
static char tempnm[] = "__.SYMDEF";
static struct ranlib rantab[TABSZ];
static int tnum;
static int debug;
static int justtouch;
static char *progname = "ranlib"; /* diagnostic prefix: basename of argv[0] */

static int nextel(FILE *af);
static void fixdate(const char *s);
static void putrantab(FILE *f);
static void stash(const struct nlist *s);
static void fixsize(void);

/* Print the command-line usage summary. */
static void usage(void)
{
    printf("Usage:\n");
    printf("    %s [-td] archive...\n", progname);
    printf("Options:\n");
    printf("    -t          Touch: update the symbol-table timestamp without rebuilding it\n");
    printf("    -d          Debug: print the symbol table as it is built\n");
}

int ranlib_run(int argc, char **argv)
{
    /* Derive the diagnostic prefix from argv[0]'s basename (fallback "ranlib"). */
    if (argc > 0 && argv[0] && argv[0][0]) {
        char *slash = strrchr(argv[0], '/');
        progname    = slash ? slash + 1 : argv[0];
    }

    /* Reset state so repeated in-process runs start clean. */
    justtouch = 0;
    debug     = 0;
    tnum      = 0;
    new       = 0;
    off       = 0;
    oldoff    = 0;

    /* check for the "-t" flag" */
    for (; argc > 1 && argv[1][0] == '-'; --argc, ++argv) {
        char *p;

        for (p = argv[1] + 1; *p; ++p)
            switch (*p) {
            case 't':
                ++justtouch;
                break;
            case 'd':
                ++debug;
                break;
            default:
                fprintf(stderr, "%s: error: unknown flag '%c'\n", progname, *p);
                exit(1);
            }
    }

    if (argc <= 1) {
        usage();
        return 1;
    }

    while (--argc > 0) {
        fi = fopen(*++argv, "r");
        if (!fi) {
            fprintf(stderr, "%s: error: cannot open %s\n", progname, *argv);
            continue;
        }
        if (fgetw(fi) != ARMAG) {
            fprintf(stderr, "%s: error: not an archive: %s\n", progname, *argv);
            fclose(fi);
            continue;
        }
        if (justtouch) {
            int len;

            fseek(fi, (long)W, 0);
            if (!fgetarhdr(fi, &archdr)) {
                fprintf(stderr, "%s: error: malformed archive: %s\n", progname, *argv);
                fclose(fi);
                continue;
            }
            len = strlen(tempnm);
            if (strncmp(archdr.ar_name, tempnm, len)) {
                fprintf(stderr, "%s: error: no symbol table: %s\n", progname, *argv);
                fclose(fi);
                continue;
            }
            fclose(fi);
            fixdate(*argv);
            continue;
        }
        new = tnum = 0;
        off        = W;
        if (nextel(fi) == 0) {
            fclose(fi);
            continue;
        }
        do {
            struct nlist sym;

            if (!strncmp(tempnm, archdr.ar_name, sizeof(archdr.ar_name)))
                continue;
            if (!fgethdr(fi, &exh))
                continue;
            if (N_BADMAG(exh))
                continue;
            if (!exh.a_syms) {
                fprintf(stderr, "%s: warning: %s(%s): no symbol table\n", progname, *argv,
                        archdr.ar_name);
                continue;
            }
            fseek(fi, 2 * (exh.a_const + exh.a_text + exh.a_data), 1);
            for (;;) {
                int n = fgetsym(fi, &sym);
                if (n == 0) { /* malloc returned 0 */
                    fprintf(stderr, "%s: error: out of memory\n", progname);
                    exit(1);
                }
                if (n == 1) /* end of symtab */
                    break;
                if ((sym.n_type & N_EXT) && (sym.n_type & N_TYPE) != N_UNDF)
                    stash(&sym);
                else
                    free(sym.n_name);
            }
        } while (nextel(fi));
        fixsize(); /* update ran_off by length of __.SYMTAB */
        fclose(fi);
        fo = fopen(tempnm, "w");
        if (!fo) {
            fprintf(stderr, "%s: error: can't create temporary\n", progname);
            exit(1);
        }
        putrantab(fo);
        fclose(fo);
        {
            int rc;

            if (new) {
                char *av[] = { "ar", "rlb", firstname, *argv, tempnm };
                rc         = ar_run(5, av);
            } else {
                char *av[] = { "ar", "rl", *argv, tempnm };
                rc         = ar_run(4, av);
            }
            if (rc)
                fprintf(stderr, "%s: error: ``ar'' failed on %s\n", progname, *argv);
            else
                fixdate(*argv);
        }
        unlink(tempnm);
    }
    return (0);
}

static int nextel(FILE *af)
{
    oldoff = off;
    fseek(af, off, 0);
    if (!fgetarhdr(af, &archdr))
        return (0);
    arsize = (archdr.ar_size + W - 1) / W * W;
    off    = ftell(af) + arsize;
    return (1);
}

static void fixdate(const char *s) /* patch time */
{
    int fd;

    fd = open(s, 2);
    if (fd < 0) {
        fprintf(stderr, "%s: error: can't reopen %s\n", progname, s);
        return;
    }
    lseek(fd, (long)W, 0);
    getarhdr(fd, &archdr);
    lseek(fd, (long)W, 0);
    archdr.ar_date = time(NULL);
    putarhdr(fd, &archdr);
    close(fd);
}

static void putrantab(FILE *f)
{
    struct ranlib *p;
    int n;

    n = 0;
    for (p = rantab; p < rantab + tnum; ++p) {
        if (debug)
            printf("%08lo: %3ld  %s\n", (long)p->ran_off, (long)p->ran_len, p->ran_name);
        fputran(p, f);
        n += 4 + p->ran_len; /* fputran writes 1 len + 3-byte half-word off + name */
        free(p->ran_name);
    }
    tnum = 0;
    /* pad with nulls */
    do
        putc(0, f);
    while (++n % W);
}

static void stash(const struct nlist *s)
{
    if (tnum >= TABSZ) {
        fprintf(stderr, "%s: error: symbol table overflow\n", progname);
        exit(1);
    }
    rantab[tnum].ran_name = s->n_name;
    rantab[tnum].ran_len  = s->n_len;
    rantab[tnum].ran_off  = oldoff;
    ++tnum;
}

static void fixsize(void)
{
    int i;
    long offdelta;

    offdelta = ARHDRSZ;
    for (i = 0; i < tnum; ++i)
        offdelta += rantab[i].ran_len + 4; /* 1 len + 3-byte half-word off + name */
    offdelta = (offdelta + W) / W * W;
    off      = W;
    nextel(fi);
    if (!strncmp(archdr.ar_name, tempnm, sizeof(archdr.ar_name))) {
        new = 0;
        offdelta -= ARHDRSZ + arsize;
    } else {
        new = 1;
        strncpy(firstname, archdr.ar_name, sizeof(firstname));
        firstname[sizeof(archdr.ar_name)] = 0;
    }
    for (i = 0; i < tnum; ++i)
        rantab[i].ran_off += offdelta;
}
