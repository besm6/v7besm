/*
 *	print symbol tables for
 *      object files (SVS-B)
 *
 *	nm [-goprun] name ...
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "besm6/ar.h"
#include "besm6/b.out.h"

#include "nm.h"

#define N_FORMAT "%05lo" /* octal printf format for a symbol value */

#define QUANT 2048

static int numsort_flg;
static int undef_flg;
static int revsort_flg;
static int globl_flg;
static int nosort_flg;
static int prep_flg;
static int ar_flg;
static char *progname = "nm"; /* diagnostic prefix: basename of argv[0] */
static long off;
static struct exec hdr;
static struct ar_hdr arhdr;
static FILE *fi;

static void nm(const char *name, int narg);
static int compare(const void *p1, const void *p2);

// Print the command-line usage summary.
static void usage(void)
{
    printf("Usage:\n");
    printf("    %s [-goprun] file...\n", progname);
    printf("Options:\n");
    printf("    -n          Sort numerically by value\n");
    printf("    -g          List global (external) symbols only\n");
    printf("    -u          List undefined symbols only\n");
    printf("    -r          Sort in reverse order\n");
    printf("    -p          Don't sort; print in symbol-table order\n");
    printf("    -o          Prepend the file name to each line\n");
}

int nm_run(int argc, char **argv)
{
    int narg;

    /* Derive the diagnostic prefix from argv[0]'s basename (fallback "nm"). */
    if (argc > 0 && argv[0] && argv[0][0]) {
        char *slash = strrchr(argv[0], '/');
        progname    = slash ? slash + 1 : argv[0];
    }

    /* Reset option state so repeated in-process runs start clean. */
    numsort_flg = undef_flg = revsort_flg = 0;
    globl_flg = nosort_flg = prep_flg = ar_flg = 0;

    if (--argc > 0 && argv[1][0] == '-' && argv[1][1] != 0) {
        argv++;
        while (*++*argv)
            switch (**argv) {
            case 'n': /* sort numerically */
                numsort_flg++;
                continue;
            case 'g': /* globl symbols only */
                globl_flg++;
                continue;
            case 'u': /* undefined symbols only */
                undef_flg++;
                continue;
            case 'r': /* sort in reverse order */
                revsort_flg++;
                continue;
            case 'p': /* don't sort -- symbol table order */
                nosort_flg++;
                continue;
            case 'o': /* prepend a name to each line */
                prep_flg++;
                continue;
            default: /* oops */
                fprintf(stderr, "%s: error: unknown flag -%c\n",
                        progname, *argv[0]);
                return 1;
            }
        argc--;
    }
    if (argc == 0) {
        usage();
        return 1;
    }
    narg = argc;
    while (argc--) {
        fi = fopen(*++argv, "r");
        if (fi == NULL) {
            fprintf(stderr, "%s: error: cannot open %s\n", progname, *argv);
            continue;
        }
        hdr.a_magic = fgetw(fi);
        ar_flg      = hdr.a_magic == ARMAG;
        if (!ar_flg && N_BADMAG(hdr)) {
            fprintf(stderr, "%s: error: %s: bad format\n", progname, *argv);
            fclose(fi);
            continue;
        }
        off = 8L;
        do {
            if (ar_flg) {
                fseek(fi, off, 0);
                if (!fgetarhdr(fi, &arhdr))
                    break;
                /* offset to next element */
                off = arhdr.ar_size + ftell(fi);
                if (narg > 1)
                    printf("\n%s:\n", *argv);
            } else
                fseek(fi, 0L, 0);
            fgethdr(fi, &hdr);
            nm(*argv, narg);
        } while (ar_flg);
        fclose(fi);
    }
    return 0;
}

static void nm(const char *name, int narg)
{
    struct nlist sym;          /* current symbol */
    struct nlist *symp = NULL; /* sym table */
    int symplen        = 0;    /* sym table length */
    int symindex       = 0;    /* next free table entry */
    int c;
    long n;

    n = hdr.a_const + hdr.a_text + hdr.a_data;
    if (!(hdr.a_flag & RELFLG))
        n *= 2;
    fseek(fi, n, 1);
    n = hdr.a_syms;
    if (n == 0) {
        fprintf(stderr, "%s: error: %s: ", progname, name);
        if (ar_flg)
            fprintf(stderr, "%s: ", arhdr.ar_name);
        fprintf(stderr, "no symbol table\n");
        return;
    }
    for (;;) {
        c = fgetsym(fi, &sym);
        if (c == 0) {
            fprintf(stderr, "%s: error: out of memory\n", progname);
            exit(4);
        }
        if (c == 1)
            break;
        n -= c;
        if (n <= 0) {
            fprintf(stderr,
                    "%s: error: bad symbol table length\n", progname);
            exit(3);
        }
        if (globl_flg && (sym.n_type & N_EXT) == 0) {
            free(sym.n_name);
            continue;
        }
        switch (sym.n_type & N_TYPE) {
        default:
        case N_ABS:
            c = 'a';
            break;
        case N_CONST:
            c = 'l';
            break;
        case N_TEXT:
            c = 't';
            break;
        case N_DATA:
            c = 'd';
            break;
        case N_BSS:
            c = 'b';
            break;
        case N_FN:
            c = 'f';
            break;
        case N_UNDF:
            c = 'u';
            break;
        case N_COMM:
            c = 'c';
            break;
        }
        if (undef_flg && c != 'u') {
            free(sym.n_name);
            continue;
        }
        if (sym.n_type & N_EXT)
            c = toupper(c);
        sym.n_type = c;
        if (symindex == symplen) {
            struct nlist *grown;
            if (!symplen) {
                symplen = QUANT;
                grown   = (struct nlist *)malloc(symplen * sizeof(struct nlist));
            } else {
                symplen += QUANT;
                grown = (struct nlist *)realloc(symp, symplen * sizeof(struct nlist));
            }
            if (grown == NULL) {
                fprintf(stderr, "%s: error: out of memory on %s\n", progname, name);
                free(symp);
                exit(2);
            }
            symp = grown;
        }
        symp[symindex++] = sym;
    }
    if (!nosort_flg)
        qsort(symp, symindex, sizeof(struct nlist), compare);
    if ((ar_flg || narg > 1) && !prep_flg) {
        printf("\n%s:", name);
        if (ar_flg)
            printf(" %s:\n", arhdr.ar_name);
        else
            printf("\n");
    }
    for (n = 0; n < symindex; n++) {
        if (prep_flg) {
            printf("%s:\t", name);
            if (ar_flg)
                printf("%s:\t", arhdr.ar_name);
        }
        c = symp[n].n_type;
        if (!undef_flg) {
            if (c == 'u' || c == 'U')
                printf("     ");
            else
                printf(N_FORMAT, (long)symp[n].n_value);
            printf(" %c ", c);
        }
        printf("%s\n", symp[n].n_name);
    }
    while (symindex)
        free(symp[--symindex].n_name);
    if (symplen)
        free((char *)symp);
}

static int compare(const void *p1, const void *p2)
{
    const struct nlist *s1 = p1;
    const struct nlist *s2 = p2;
    int rez;

    if (numsort_flg) {
        long d = s1->n_value - s2->n_value;

        if (d > 0)
            rez = 1;
        else if (d == 0)
            rez = 0;
        else
            rez = -1;
    } else {
        rez = strcmp(s1->n_name, s2->n_name);
    }
    if (revsort_flg)
        rez = -rez;
    return (rez);
}
