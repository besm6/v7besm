/*
 *      Maintenance of library (archive) files.
 *
 *      ar [mrxtdpq][uvnbail] archive_name files ...
 *
 *      Build method:  cc -O -n -s
 *
 */

#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "besm6/ar.h"
#include "besm6/b.out.h"

#include "archive.h"

#define W 6 // BESM-6 word length in bytes

#define SKIP 1
#define IODD 2
#define OODD 4
#define HEAD 8

static char *man = "mrxtdpq";
static char *opt = "uvnbail";

static int signum[] = { SIGHUP, SIGINT, SIGQUIT, 0 };

// All mutable per-run state, bundled so that reset_state() can clear it in one
// memset and repeated ar_run() calls do not depend on one another.
struct arstate {
    struct stat   stbuf;
    struct ar_hdr arbuf;

    void (*comfun)(void);
    char   flg[26];
    char **namv;
    int    namc;
    char  *arnam;
    char  *ponam;

    // Temporary file name templates (mkstemp replaces the last six 'X' chars).
    char   tmp0nam[20];
    char   tmp1nam[20];
    char   tmp2nam[20];
    char  *tfnam;
    char  *tf1nam;
    char  *tf2nam;

    char  *file;
    char   name[31];
    int    af;
    int    tf;
    int    tf1;
    int    tf2;
    int    qf;
    int    bastate;
    char   buf[512];

    // Single exit point: done() unwinds via longjmp back into ar_run() so that
    // the engine can be invoked repeatedly within one process (from tests).
    jmp_buf done_env;
    int     exit_code;
};

static struct arstate ar;


static void done(int c);
static void sigdone(int sig);
static void wrerr(void);
static void usage(void);
static void noar(void);
static int getaf(void);
static void getqf(void);
static int getdir(void);
static int match(void);
static void bamatch(void);
static int stats(void);
static int notfound(void);
static int morefil(void);
static char *trim(char *s);
static void longt(void);
static void pmode(void);
static void selmode(const int *pairp);
static void copyfil(int fi, int fo, int flag);
static void movefil(int f);
static void init(void);
static void cleanup(void);
static void install(void);
static void mesg(int c);
static void phserr(void);
static void setcom(void (*fun)(void));
static void rcommand(void);
static void dcommand(void);
static void xcommand(void);
static void tcommand(void);
static void pcommand(void);
static void mcommand(void);
static void qcommand(void);

// Reset global state so that repeated ar_run() calls do not depend on
// one another.
static void reset_state(void)
{
    memset(&ar, 0, sizeof(ar));
    strcpy(ar.tmp0nam, "/tmp/ar0XXXXXX");
    strcpy(ar.tmp1nam, "/tmp/ar1XXXXXX");
    strcpy(ar.tmp2nam, "/tmp/ar2XXXXXX");
}

int ar_run(int argc, char **argv)
{
    int i;
    char *cp;

    reset_state();
    ar.exit_code = 0;
    if (setjmp(ar.done_env))
        return ar.exit_code;

    for (i = 0; signum[i]; i++)
        if (signal(signum[i], SIG_IGN) != SIG_IGN)
            signal(signum[i], sigdone);
    if (argc < 3)
        usage();
    for (cp = argv[1]; *cp; cp++)
        switch (*cp) {
        case 'l':
        case 'v':
        case 'u':
        case 'n':
        case 'a':
        case 'b':
        case 'c':
        case 'i':
            ar.flg[*cp - 'a']++;
            continue;

        case 'r':
            setcom(rcommand);
            continue;

        case 'd':
            setcom(dcommand);
            continue;

        case 'x':
            setcom(xcommand);
            continue;

        case 't':
            setcom(tcommand);
            continue;

        case 'p':
            setcom(pcommand);
            continue;

        case 'm':
            setcom(mcommand);
            continue;

        case 'q':
            setcom(qcommand);
            continue;

        default:
            fprintf(stderr, "ar: unknown flag `%c'\n", *cp);
            done(1);
        }
    if (ar.flg['l' - 'a']) {
        strcpy(ar.tmp0nam, "ar0XXXXXX");
        strcpy(ar.tmp1nam, "ar1XXXXXX");
        strcpy(ar.tmp2nam, "ar2XXXXXX");
    }
    if (ar.flg['i' - 'a'])
        ar.flg['b' - 'a']++;
    // cppcheck-suppress duplicateExpression
    if (ar.flg['a' - 'a'] || ar.flg['b' - 'a']) {
        ar.bastate = 1;
        ar.ponam   = trim(argv[2]);
        argv++;
        argc--;
        if (argc < 3)
            usage();
    }
    ar.arnam = argv[2];
    ar.namv  = argv + 3;
    ar.namc  = argc - 3;
    if (ar.comfun == 0) {
        if (ar.flg['u' - 'a'] == 0) {
            fprintf(stderr, "ar: must be one of [%s]\n",
                    man);
            done(1);
        }
        setcom(rcommand);
    }
    (*ar.comfun)();
    done(notfound());
    return ar.exit_code;
}

static void setcom(void (*fun)(void))
{
    if (ar.comfun != 0) {
        fprintf(stderr, "ar: only one of [%s] allowed\n",
                man);
        done(1);
    }
    ar.comfun = fun;
}

static void rcommand(void)
{
    int f;

    init();
    getaf();
    while (!getdir()) {
        bamatch();
        if (ar.namc == 0 || match()) {
            f = stats();
            if (f < 0) {
                if (ar.namc)
                    fprintf(stderr, "ar: cannot open %s\n", ar.file);
                goto cp;
            }
            if (ar.flg['u' - 'a'])
                if (ar.stbuf.st_mtime <= ar.arbuf.ar_date) {
                    close(f);
                    goto cp;
                }
            mesg('r');
            copyfil(ar.af, -1, IODD + SKIP);
            movefil(f);
            continue;
        }
    cp:
        mesg('c');
        copyfil(ar.af, ar.tf, IODD + OODD + HEAD);
    }
    cleanup();
}

static void dcommand(void)
{
    init();
    if (getaf())
        noar();
    while (!getdir()) {
        if (match()) {
            mesg('d');
            copyfil(ar.af, -1, IODD + SKIP);
            continue;
        }
        mesg('c');
        copyfil(ar.af, ar.tf, IODD + OODD + HEAD);
    }
    install();
}

static void xcommand(void)
{
    int f;

    if (getaf())
        noar();
    while (!getdir()) {
        if (ar.namc == 0 || match()) {
            f = creat(ar.file, ar.arbuf.ar_mode & 0777);
            if (f < 0) {
                fprintf(stderr, "ar: cannot create %s\n", ar.file);
                goto sk;
            }
            mesg('x');
            copyfil(ar.af, f, IODD);
            close(f);
            continue;
        }
    sk:
        mesg('c');
        copyfil(ar.af, -1, IODD + SKIP);
        if (ar.namc > 0 && !morefil())
            done(0);
    }
}

static void pcommand(void)
{
    if (getaf())
        noar();
    while (!getdir()) {
        if (ar.namc == 0 || match()) {
            if (ar.flg['v' - 'a']) {
                printf("\n<%s>\n\n", ar.file);
                fflush(stdout);
            }
            copyfil(ar.af, 1, IODD);
            continue;
        }
        copyfil(ar.af, -1, IODD + SKIP);
    }
}

static void mcommand(void)
{
    init();
    if (getaf())
        noar();
    ar.tf2 = mkstemp(ar.tmp2nam);
    if (ar.tf2 < 0) {
        fprintf(stderr, "ar: cannot create third temporary file\n");
        done(1);
    }
    ar.tf2nam = ar.tmp2nam;
    while (!getdir()) {
        bamatch();
        if (match()) {
            mesg('m');
            copyfil(ar.af, ar.tf2, IODD + OODD + HEAD);
            continue;
        }
        mesg('c');
        copyfil(ar.af, ar.tf, IODD + OODD + HEAD);
    }
    install();
}

static void tcommand(void)
{
    if (getaf())
        noar();
    while (!getdir()) {
        if (ar.namc == 0 || match()) {
            if (ar.flg['v' - 'a'])
                longt();
            printf("%s\n", trim(ar.file));
        }
        copyfil(ar.af, -1, IODD + SKIP);
    }
}

static void qcommand(void)
{
    int i, f;

    // cppcheck-suppress duplicateExpression
    if (ar.flg['a' - 'a'] || ar.flg['b' - 'a']) {
        fprintf(stderr, "ar: abi and q incompatible\n");
        done(1);
    }
    getqf();
    for (i = 0; signum[i]; i++)
        signal(signum[i], SIG_IGN);
    lseek(ar.qf, 0l, SEEK_END);
    for (i = 0; i < ar.namc; i++) {
        ar.file = ar.namv[i];
        if (ar.file == 0)
            continue;
        ar.namv[i] = 0;
        mesg('q');
        f = stats();
        if (f < 0) {
            fprintf(stderr, "ar: cannot open %s\n", ar.file);
            continue;
        }
        ar.tf = ar.qf;
        movefil(f);
        ar.qf = ar.tf;
    }
}

static void init(void)
{
    uword_t mbuf = ARMAG;

    ar.tf = mkstemp(ar.tmp0nam);
    if (ar.tf < 0) {
        fprintf(stderr,
                "ar: cannot create temporary file\n");
        done(1);
    }
    ar.tfnam = ar.tmp0nam;
    if (!putint(ar.tf, mbuf))
        wrerr();
}

static int getaf(void)
{
    uword_t mbuf;

    ar.af = open(ar.arnam, O_RDONLY);
    if (ar.af < 0)
        return (1);
    if (!getint(ar.af, &mbuf) || mbuf != ARMAG) {
        fprintf(stderr, "ar: %s is not in archive format\n",
                ar.arnam);
        done(1);
    }
    return (0);
}

static void getqf(void)
{
    uword_t mbuf;

    if ((ar.qf = open(ar.arnam, O_RDWR)) < 0) {
        if (!ar.flg['c' - 'a'])
            fprintf(stderr, "ar: creating %s\n", ar.arnam);
        close(creat(ar.arnam, 0666));
        if ((ar.qf = open(ar.arnam, O_RDWR)) < 0) {
            fprintf(stderr, "ar: cannot create %s\n", ar.arnam);
            done(1);
        }
        mbuf = ARMAG;
        if (!putint(ar.qf, mbuf))
            wrerr();
    } else if (!getint(ar.qf, &mbuf) || mbuf != ARMAG) {
        fprintf(stderr, "ar: %s is not in archive format\n",
                ar.arnam);
        done(1);
    }
}

static void usage(void)
{
    printf("Usage: ar [%s][%s] archive file...\n", opt,
           man);
    done(1);
}

static void noar(void)
{
    fprintf(stderr, "ar: %s not found\n", ar.arnam);
    done(1);
}

static void sigdone(int sig)
{
    (void)sig;
    done(100);
}

static void done(int c)
{
    if (ar.tfnam)
        unlink(ar.tfnam);
    if (ar.tf1nam)
        unlink(ar.tf1nam);
    if (ar.tf2nam)
        unlink(ar.tf2nam);
    ar.exit_code = c;
    longjmp(ar.done_env, 1);
}

static int notfound(void)
{
    int i, n;

    n = 0;
    for (i = 0; i < ar.namc; i++)
        if (ar.namv[i]) {
            fprintf(stderr, "ar: %s not found\n", ar.namv[i]);
            n++;
        }
    return (n);
}

static int morefil(void)
{
    int i, n;

    n = 0;
    for (i = 0; i < ar.namc; i++)
        if (ar.namv[i])
            n++;
    return (n);
}

static void cleanup(void)
{
    int i, f;

    for (i = 0; i < ar.namc; i++) {
        ar.file = ar.namv[i];
        if (ar.file == 0)
            continue;
        ar.namv[i] = 0;
        mesg('a');
        f = stats();
        if (f < 0) {
            fprintf(stderr, "ar: cannot open %s\n", ar.file);
            continue;
        }
        movefil(f);
    }
    install();
}

static void install(void)
{
    int i;

    for (i = 0; signum[i]; i++)
        signal(signum[i], SIG_IGN);
    if (ar.af < 0)
        if (!ar.flg['c' - 'a'])
            fprintf(stderr, "ar: creating %s\n", ar.arnam);
    close(ar.af);
    ar.af = creat(ar.arnam, 0666);
    if (ar.af < 0) {
        fprintf(stderr, "ar: cannot create %s\n", ar.arnam);
        done(1);
    }
    if (ar.tfnam) {
        lseek(ar.tf, 0l, SEEK_SET);
        while ((i = read(ar.tf, ar.buf, 512)) > 0)
            if (write(ar.af, ar.buf, i) != i)
                wrerr();
    }
    if (ar.tf2nam) {
        lseek(ar.tf2, 0l, SEEK_SET);
        while ((i = read(ar.tf2, ar.buf, 512)) > 0)
            if (write(ar.af, ar.buf, i) != i)
                wrerr();
    }
    if (ar.tf1nam) {
        lseek(ar.tf1, 0l, SEEK_SET);
        while ((i = read(ar.tf1, ar.buf, 512)) > 0)
            if (write(ar.af, ar.buf, i) != i)
                wrerr();
    }
}

/*
 * insert the file 'file'
 * into the temporary file
 */
static void movefil(int f)
{
    const char *cp;
    int i;

    cp = trim(ar.file);
    for (i = 0; i < (int)sizeof(ar.arbuf.ar_name); i++)
        if ((ar.arbuf.ar_name[i] = *cp))
            cp++;
    ar.arbuf.ar_size = ar.stbuf.st_size;
    ar.arbuf.ar_date = ar.stbuf.st_mtime;
    ar.arbuf.ar_uid  = ar.stbuf.st_uid;
    ar.arbuf.ar_gid  = ar.stbuf.st_gid;
    ar.arbuf.ar_mode = ar.stbuf.st_mode;
    copyfil(f, ar.tf, OODD + HEAD);
    close(f);
}

static int stats(void)
{
    int f;

    f = open(ar.file, O_RDONLY);
    if (f < 0)
        return (f);
    if (fstat(f, &ar.stbuf) < 0) {
        close(f);
        return (-1);
    }
    return (f);
}

/*
 * copy next file
 * size given in arbuf
 *
 * An archive member is zero-padded to a whole BESM-6 word (W=6 bytes), and the
 * already-aligned size is written into the header: ld steps to the next member
 * by `ar_size + ARHDRSZ` without rounding, so ar_size must be a multiple of W.
 */
static void copyfil(int fi, int fo, int flag)
{
    int pe;
    long size = ar.arbuf.ar_size; // actual number of data bytes
    int pad   = (int)((W - size % W) % W); // padding to the word boundary (0..W-1)

    if (flag & HEAD) {
        ar.arbuf.ar_size = size + pad;
        if (!putarhdr(fo, &ar.arbuf))
            wrerr();
    }
    pe = 0;
    while (size > 0) {
        int i, o;
        i = o = (size < 512) ? (int)size : 512;
        if (read(fi, ar.buf, i) != i)
            pe++;
        if ((flag & SKIP) == 0)
            if (write(fo, ar.buf, o) != o)
                wrerr();
        size -= 512;
    }
    if (pad) {
        if (flag & IODD)
            if (read(fi, ar.buf, pad) != pad) // consume the padding from the input
                pe++;
        if ((flag & OODD) && (flag & SKIP) == 0) {
            char zero[W];
            memset(zero, 0, sizeof(zero));
            if (write(fo, zero, pad) != pad)
                wrerr();
        }
    }
    if (pe)
        phserr();
}

static int getdir(void)
{
    int i;

    if (!getarhdr(ar.af, &ar.arbuf)) {
        if (ar.tf1nam) {
            i      = ar.tf;
            ar.tf  = ar.tf1;
            ar.tf1 = i;
        }
        return (1);
    }
    for (i = 0; i < (int)sizeof(ar.arbuf.ar_name); i++)
        ar.name[i] = ar.arbuf.ar_name[i];
    ar.file = ar.name;
    return (0);
}

static int match(void)
{
    int i;

    for (i = 0; i < ar.namc; i++) {
        if (ar.namv[i] == 0)
            continue;
        if (strcmp(trim(ar.namv[i]), ar.file) == 0) {
            ar.file    = ar.namv[i];
            ar.namv[i] = 0;
            return (1);
        }
    }
    return (0);
}

static void bamatch(void)
{
    int f;

    switch (ar.bastate) {
    case 1:
        if (strcmp(ar.file, ar.ponam) != 0)
            return;
        ar.bastate = 2;
        // cppcheck-suppress duplicateExpression
        if (ar.flg['a' - 'a'])
            return;
        /* fallthrough */

    case 2:
        ar.bastate = 0;
        f          = mkstemp(ar.tmp1nam);
        if (f < 0) {
            fprintf(stderr, "ar: cannot create second temporary file\n");
            return;
        }
        ar.tf1nam = ar.tmp1nam;
        ar.tf1    = ar.tf;
        ar.tf     = f;
    }
}

static void phserr(void)
{
    fprintf(stderr, "ar: phase error on %s\n", ar.file);
}

static void mesg(int c)
{
    if (ar.flg['v' - 'a'])
        if (c != 'c' || ar.flg['v' - 'a'] > 1)
            printf("%c - %s\n", c, ar.file);
}

static char *trim(char *s)
{
    char *p1, *p2;

    for (p1 = s; *p1; p1++)
        ;
    while (p1 > s) {
        if (*--p1 != '/')
            break;
        *p1 = 0;
    }
    p2 = s;
    for (p1 = s; *p1; p1++)
        if (*p1 == '/')
            p2 = p1 + 1;
    return (p2);
}

#define IFMT  060000
#define ISARG 01000
#define LARGE 010000
#define SUID  04000
#define SGID  02000
#define ROWN  0400
#define WOWN  0200
#define XOWN  0100
#define RGRP  040
#define WGRP  020
#define XGRP  010
#define ROTH  04
#define WOTH  02
#define XOTH  01
#define STXT  01000

static void longt(void)
{
    const char *cp;
    time_t t;

    pmode();
    printf("%3d/%1d", (int)ar.arbuf.ar_uid, (int)ar.arbuf.ar_gid);
    printf("%7ld", (long)ar.arbuf.ar_size);
    t  = ar.arbuf.ar_date;
    cp = ctime(&t);
    printf(" %-12.12s %-4.4s ", cp + 4, cp + 20);
}

static int m1[] = { 1, ROWN, 'r', '-' };
static int m2[] = { 1, WOWN, 'w', '-' };
static int m3[] = { 2, SUID, 's', XOWN, 'x', '-' };
static int m4[] = { 1, RGRP, 'r', '-' };
static int m5[] = { 1, WGRP, 'w', '-' };
static int m6[] = { 2, SGID, 's', XGRP, 'x', '-' };
static int m7[] = { 1, ROTH, 'r', '-' };
static int m8[] = { 1, WOTH, 'w', '-' };
static int m9[] = { 2, STXT, 't', XOTH, 'x', '-' };

static int *m[] = { m1, m2, m3, m4, m5, m6, m7, m8, m9 };

static void pmode(void)
{
    int **mp;

    for (mp = &m[0]; mp < &m[9];)
        selmode(*mp++);
}

static void selmode(const int *pairp)
{
    int n;
    const int *ap;

    ap = pairp;
    n  = *ap++;
    while (--n >= 0 && (ar.arbuf.ar_mode & *ap++) == 0)
        ap++;
    putchar(*ap);
}

static void wrerr(void)
{
    perror("ar write error");
    done(1);
}
