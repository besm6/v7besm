/*
 *      strip file ...          - remove the symbol table and relocation bits
 *                                from BESM-6 a.out objects, rewriting each file
 *                                in place via a temporary file.
 */

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "besm6/b.out.h"

#include "strip.h"

#define BUFSZ 8192 /* copy chunk size in bytes */

static char *progname = "strip"; /* diagnostic prefix: basename of argv[0] */

/* Copy `size` bytes from `fr` to `to`; returns nonzero on short read/write. */
static int copy(const char *name, FILE *fr, FILE *to, long size)
{
    char buf[BUFSZ];

    while (size != 0) {
        size_t s = sizeof(buf);
        if (size < (long)s)
            s = (size_t)size;
        if (fread(buf, 1, s, fr) != s) {
            fprintf(stderr, "%s: error: %s unexpected eof\n", progname, name);
            return 1;
        }
        if (fwrite(buf, 1, s, to) != s) {
            fprintf(stderr, "%s: error: %s unexpected write eof\n", progname, name);
            return 1;
        }
        size -= (long)s;
    }
    return 0;
}

/*
 * Strip one object, rewriting it in place through the scratch file `tf`.
 * Returns 0 on success, 1 on a soft error (file left untouched), 2 on a hard
 * error (the file may have been clobbered).
 */
static int strip_file(const char *name, FILE *tf)
{
    struct exec head;
    FILE *f;
    long size;
    int status = 0;

    f = fopen(name, "r");
    if (!f) {
        fprintf(stderr, "%s: error: cannot open %s\n", progname, name);
        return 1;
    }
    if (!fgethdr(f, &head) || N_BADMAG(head)) {
        fprintf(stderr, "%s: error: %s not in a.out format\n", progname, name);
        fclose(f);
        return 1;
    }
    if (!head.a_syms && (head.a_flag & RELFLG)) {
        fprintf(stderr, "%s: error: %s already stripped\n", progname, name);
        fclose(f);
        return 0;
    }
    size        = head.a_const + head.a_text + head.a_data;
    head.a_syms = 0;
    head.a_flag |= RELFLG;
    fseek(tf, 0L, SEEK_SET);
    fputhdr(&head, tf);
    if (copy(name, f, tf, size)) {
        fclose(f);
        return 1;
    }
    size += HDRSZ;
    fclose(f);
    f = fopen(name, "w");
    if (!f) {
        fprintf(stderr, "%s: error: %s cannot recreate\n", progname, name);
        return 1;
    }
    fseek(tf, 0L, SEEK_SET);
    if (copy(name, tf, f, size))
        status = 2;
    fclose(f);
    return status;
}

/* Print the command-line usage summary. */
static void usage(void)
{
    printf("Usage:\n");
    printf("    %s file...\n", progname);
}

int strip_run(int argc, char **argv)
{
    char tname[] = "/tmp/stripXXXXXX";
    FILE *tf;
    int fd;
    int status = 0;
    int i;

    /* Derive the diagnostic prefix from argv[0]'s basename (fallback "strip"). */
    if (argc > 0 && argv[0] && argv[0][0]) {
        char *slash = strrchr(argv[0], '/');
        progname    = slash ? slash + 1 : argv[0];
    }

    if (argc < 2) {
        usage();
        return 1;
    }

    signal(SIGHUP, SIG_IGN);
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);

    fd = mkstemp(tname);
    if (fd < 0 || (tf = fdopen(fd, "w+")) == NULL) {
        fprintf(stderr, "%s: error: cannot create temp file\n", progname);
        if (fd >= 0) {
            close(fd);
            unlink(tname);
        }
        return 2;
    }
    for (i = 1; i < argc; i++) {
        int s = strip_file(argv[i], tf);
        if (s > status)
            status = s;
        if (status > 1)
            break;
    }
    fclose(tf);
    unlink(tname);
    return status;
}
