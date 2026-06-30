/*
 *      strip [file ...]        - remove the symbol table and relocation bits
 *                                from BESM-6 a.out objects, rewriting each file
 *                                in place via a temporary file.
 */

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "besm6/b.out.h"

#include "strip.h"

#define BUFSZ 8192 /* copy chunk size in bytes */

/* Copy `size` bytes from `fr` to `to`; returns nonzero on short read/write. */
static int copy(const char *name, FILE *fr, FILE *to, long size)
{
    char buf[BUFSZ];

    while (size != 0) {
        size_t s = sizeof(buf);
        if (size < (long)s)
            s = (size_t)size;
        if (fread(buf, 1, s, fr) != s) {
            printf("%s unexpected eof\n", name);
            return 1;
        }
        if (fwrite(buf, 1, s, to) != s) {
            printf("%s unexpected write eof\n", name);
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
        printf("cannot open %s\n", name);
        return 1;
    }
    if (!fgethdr(f, &head) || N_BADMAG(head)) {
        printf("%s not in a.out format\n", name);
        fclose(f);
        return 1;
    }
    if (!head.a_syms && (head.a_flag & RELFLG)) {
        printf("%s already stripped\n", name);
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
        printf("%s cannot recreate\n", name);
        return 1;
    }
    fseek(tf, 0L, SEEK_SET);
    if (copy(name, tf, f, size))
        status = 2;
    fclose(f);
    return status;
}

int strip_run(int argc, char **argv)
{
    char tname[] = "/tmp/stripXXXXXX";
    FILE *tf;
    int fd;
    int status = 0;
    int i;

    signal(SIGHUP, SIG_IGN);
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);

    fd = mkstemp(tname);
    if (fd < 0 || (tf = fdopen(fd, "w+")) == NULL) {
        printf("cannot create temp file\n");
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
