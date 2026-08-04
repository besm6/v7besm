//
//      DEMOS SVS-B operating system.
//
//      Build the symdef table for fast loading.
//
//      Author: S. Vakulenko.
//      Version of 06.02.90.
//
//      Source code taken from UNIX 4.3 BSD.
//

#include "symdef.h"

#include <fcntl.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "besm6/ar.h"
#include "besm6/b.out.h"
#include "besm6/ranlib.h"

#include "archive.h" // ar_run() -- the in-process archiver engine

#define W 6 // sizeof word of BESM-6

//
// Capacity of the __.SYMDEF table this program builds, in entries.
//
// The BESM-6 value is not an address-space cut -- a struct ranlib is three words,
// so even the host's 1000 is only 3,000 words of bss in a program that has 28,672
// -- it is the number b6ld will READ.  cmd/ld/intern.h caps its own rantab[] at
// RANTABSZ 512 on this target, and an index longer than that is one the machine's
// own linker refuses, so writing one would be a trap with nothing to catch it.
// 512 against a measured peak of 331 (build/kernel/libunix.a; libc.a is 222 and
// libcurses.a 154), and stash() below fails loudly rather than overruns.
//
#if besm6
#   define TABSZ 512
#else
#   define TABSZ 1000
#endif

static struct ar_hdr archdr;
static struct exec exh;

static long arsize;
static FILE *fi, *fo;
static long off, oldoff;
static int new;
static char firstname[ARMAXNAME + 1];
static char tempnm[] = "__.SYMDEF";
static struct ranlib rantab[TABSZ];
static int tnum;
static int debug;
static int justtouch;
static char *progname = "ranlib"; // diagnostic prefix: basename of argv[0]

// Single exit point, exactly as cmd/ar's finish() is: fail() unwinds to the
// setjmp() in ranlib_run() instead of calling exit().
static jmp_buf done_env;
static int exit_code;
static int made_temp; // is the __.SYMDEF in the cwd one we created?

static int nextel(FILE *af);
static void fixdate(const char *s);
static void putrantab(FILE *f);
static void stash(const struct nlist *s);
static void fixsize(void);
static void fail(int c);

// Close a stream and forget it, so that fail() below cannot close it twice.
static void closein(void)
{
    if (fi) {
        fclose(fi);
        fi = NULL;
    }
}

static void closeout(void)
{
    if (fo) {
        fclose(fo);
        fo = NULL;
    }
}

// The single exit point, and cmd/ar's finish() in miniature.
//
// ranlib_run() is a library entry point that promises to return the exit code
// rather than call exit() (symdef.h), so that it can be run repeatedly in one
// process -- the unit tests do, and the four exit(1)s this replaced broke that
// promise. It also cleans up on the way out, which they did not: the streams
// are closed and the scratch __.SYMDEF removed. The made_temp guard is what
// keeps a flag error from deleting a __.SYMDEF this program never made.
static void fail(int c)
{
    closein();
    closeout();
    if (made_temp) {
        unlink(tempnm);
        made_temp = 0;
    }
    exit_code = c;
    longjmp(done_env, 1);
}

// Print the command-line usage summary.
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
    // Derive the diagnostic prefix from argv[0]'s basename (fallback "ranlib").
    if (argc > 0 && argv[0] && argv[0][0]) {
        char *slash = strrchr(argv[0], '/');
        progname    = slash ? slash + 1 : argv[0];
    }

    // Reset state so repeated in-process runs start clean.
    justtouch = 0;
    debug     = 0;
    tnum      = 0;
    new       = 0;
    off       = 0;
    oldoff    = 0;
    fi        = NULL;
    fo        = NULL;
    made_temp = 0;

    // fail() lands here; exit_code holds the result. Same shape as ar_run()'s
    // setjmp, and for the same reason: this engine is a library, so a fatal
    // error has to unwind rather than take the process with it.
    exit_code = 0;
    if (setjmp(done_env))
        return exit_code;

    // check for the "-t" flag"
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
                fail(1);
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
            closein();
            continue;
        }
        if (justtouch) {
            fseek(fi, (long)W, 0);
            free(archdr.ar_name); // release the previous member name, if any
            archdr.ar_name = NULL;
            if (!fgetarhdr(fi, &archdr)) {
                fprintf(stderr, "%s: error: malformed archive: %s\n", progname, *argv);
                closein();
                continue;
            }
            if (strcmp(archdr.ar_name, tempnm)) {
                fprintf(stderr, "%s: error: no symbol table: %s\n", progname, *argv);
                closein();
                continue;
            }
            closein();
            fixdate(*argv);
            continue;
        }
        new = tnum = 0;
        off        = W;
        if (nextel(fi) == 0) {
            closein();
            continue;
        }
        do {
            struct nlist sym;

            if (!strcmp(tempnm, archdr.ar_name))
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
                if (n == 0) { // malloc returned 0
                    fprintf(stderr, "%s: error: out of memory\n", progname);
                    fail(1);
                }
                if (n == 1) // end of symtab
                    break;
                if ((sym.n_type & N_EXT) && (sym.n_type & N_TYPE) != N_UNDF)
                    stash(&sym);
                else
                    free(sym.n_name);
            }
        } while (nextel(fi));
        fixsize(); // update ran_off by length of __.SYMTAB
        closein();
        fo = fopen(tempnm, "w");
        if (!fo) {
            fprintf(stderr, "%s: error: can't create temporary\n", progname);
            fail(1);
        }
        made_temp = 1;
        putrantab(fo);
        closeout();
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
        made_temp = 0;
    }
    return (0);
}

static int nextel(FILE *af)
{
    oldoff = off;
    fseek(af, off, 0);
    free(archdr.ar_name); // release the previous member name, if any
    archdr.ar_name = NULL;
    if (!fgetarhdr(af, &archdr))
        return (0);
    arsize = (archdr.ar_size + W - 1) / W * W;
    off    = ftell(af) + arsize;
    return (1);
}

static void fixdate(const char *s) // patch time
{
    int fd;

    fd = open(s, 2);
    if (fd < 0) {
        fprintf(stderr, "%s: error: can't reopen %s\n", progname, s);
        return;
    }
    lseek(fd, (long)W, 0);
    free(archdr.ar_name); // release the previous member name, if any
    archdr.ar_name = NULL;
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
        n += 4 + p->ran_len; // fputran writes 1 len + 3-byte half-word off + name
        free(p->ran_name);
    }
    tnum = 0;
    // pad with nulls
    do
        putc(0, f);
    while (++n % W);
}

static void stash(const struct nlist *s)
{
    if (tnum >= TABSZ) {
        fprintf(stderr, "%s: error: symbol table overflow\n", progname);
        fail(1);
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
    struct ar_hdr symdef;

    // On-disk header size of the "__.SYMDEF" member we are about to insert.
    symdef.ar_name = tempnm;
    offdelta       = arhdrsz(&symdef);
    for (i = 0; i < tnum; ++i)
        offdelta += rantab[i].ran_len + 4; // 1 len + 3-byte half-word off + name
    offdelta = (offdelta + W) / W * W;
    off      = W;
    nextel(fi);
    if (!strcmp(archdr.ar_name, tempnm)) {
        new = 0;
        offdelta -= arhdrsz(&archdr) + arsize;
    } else {
        new = 1;
        strncpy(firstname, archdr.ar_name, sizeof(firstname) - 1);
        firstname[sizeof(firstname) - 1] = 0;
    }
    for (i = 0; i < tnum; ++i)
        rantab[i].ran_off += offdelta;
}
