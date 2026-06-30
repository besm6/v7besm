/*
 *      Обслуживание библиотечных (архивных) файлов.
 *
 *      ar [mrxtdpq][uvnbail] имя_архива файлы ...
 *
 *      Метод сборки:  cc -O -n -s
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

#define W 6 // длина слова БЭСМ-6 в байтах

struct stat stbuf;
struct ar_hdr arbuf;

#define SKIP 1
#define IODD 2
#define OODD 4
#define HEAD 8

static char *man = "mrxtdpq";
static char *opt = "uvnbail";

static int signum[] = { SIGHUP, SIGINT, SIGQUIT, 0 };

static void (*comfun)(void);
static char flg[26];
static char **namv;
static int namc;
static char *arnam;
static char *ponam;

// Шаблоны имён временных файлов (mkstemp заменяет последние шесть 'X').
static char tmp0nam[20];
static char tmp1nam[20];
static char tmp2nam[20];
static char *tfnam;
static char *tf1nam;
static char *tf2nam;
static char *file;
static char name[31];
static int af;
static int tf;
static int tf1;
static int tf2;
static int qf;
static int bastate;
static char buf[512];

// Единственная точка выхода: done() сворачивает работу через longjmp в ar_run(),
// чтобы движок можно было многократно вызывать в одном процессе (из тестов).
static jmp_buf done_env;
static int exit_code;

static char msg;

#define MSG(l, r) (msg ? (r) : (l))

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

static void initmsg(void)
{
    const char *p;

    msg = (p = getenv("MSG")) && *p == 'r';
}

// Сброс глобального состояния, чтобы повторные вызовы ar_run() не зависели друг
// от друга.
static void reset_state(void)
{
    comfun = 0;
    memset(flg, 0, sizeof(flg));
    namv    = 0;
    namc    = 0;
    arnam   = 0;
    ponam   = 0;
    tfnam   = 0;
    tf1nam  = 0;
    tf2nam  = 0;
    file    = 0;
    af      = 0;
    tf      = 0;
    tf1     = 0;
    tf2     = 0;
    qf      = 0;
    bastate = 0;
    strcpy(tmp0nam, "/tmp/ar0XXXXXX");
    strcpy(tmp1nam, "/tmp/ar1XXXXXX");
    strcpy(tmp2nam, "/tmp/ar2XXXXXX");
}

int ar_run(int argc, char **argv)
{
    int i;
    char *cp;

    reset_state();
    exit_code = 0;
    if (setjmp(done_env))
        return exit_code;

    initmsg();
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
            flg[*cp - 'a']++;
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
            fprintf(stderr, MSG("ar: unknown flag `%c'\n", "ar: неизвестный флаг `%c'\n"), *cp);
            done(1);
        }
    if (flg['l' - 'a']) {
        strcpy(tmp0nam, "ar0XXXXXX");
        strcpy(tmp1nam, "ar1XXXXXX");
        strcpy(tmp2nam, "ar2XXXXXX");
    }
    if (flg['i' - 'a'])
        flg['b' - 'a']++;
    // cppcheck-suppress duplicateExpression
    if (flg['a' - 'a'] || flg['b' - 'a']) {
        bastate = 1;
        ponam   = trim(argv[2]);
        argv++;
        argc--;
        if (argc < 3)
            usage();
    }
    arnam = argv[2];
    namv  = argv + 3;
    namc  = argc - 3;
    if (comfun == 0) {
        if (flg['u' - 'a'] == 0) {
            fprintf(stderr, MSG("ar: must be one of [%s]\n", "ar: должен быть один из [%s]\n"),
                    man);
            done(1);
        }
        setcom(rcommand);
    }
    (*comfun)();
    done(notfound());
    return exit_code;
}

static void setcom(void (*fun)(void))
{
    if (comfun != 0) {
        fprintf(stderr, MSG("ar: only one of [%s] allowed\n", "ar: разрешен только один из [%s]\n"),
                man);
        done(1);
    }
    comfun = fun;
}

static void rcommand(void)
{
    int f;

    init();
    getaf();
    while (!getdir()) {
        bamatch();
        if (namc == 0 || match()) {
            f = stats();
            if (f < 0) {
                if (namc)
                    fprintf(stderr, MSG("ar: cannot open %s\n", "ar: не могу открыть %s\n"), file);
                goto cp;
            }
            if (flg['u' - 'a'])
                if (stbuf.st_mtime <= arbuf.ar_date) {
                    close(f);
                    goto cp;
                }
            mesg('r');
            copyfil(af, -1, IODD + SKIP);
            movefil(f);
            continue;
        }
    cp:
        mesg('c');
        copyfil(af, tf, IODD + OODD + HEAD);
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
            copyfil(af, -1, IODD + SKIP);
            continue;
        }
        mesg('c');
        copyfil(af, tf, IODD + OODD + HEAD);
    }
    install();
}

static void xcommand(void)
{
    int f;

    if (getaf())
        noar();
    while (!getdir()) {
        if (namc == 0 || match()) {
            f = creat(file, arbuf.ar_mode & 0777);
            if (f < 0) {
                fprintf(stderr, MSG("ar: cannot create %s\n", "ar: не могу создать %s\n"), file);
                goto sk;
            }
            mesg('x');
            copyfil(af, f, IODD);
            close(f);
            continue;
        }
    sk:
        mesg('c');
        copyfil(af, -1, IODD + SKIP);
        if (namc > 0 && !morefil())
            done(0);
    }
}

static void pcommand(void)
{
    if (getaf())
        noar();
    while (!getdir()) {
        if (namc == 0 || match()) {
            if (flg['v' - 'a']) {
                printf("\n<%s>\n\n", file);
                fflush(stdout);
            }
            copyfil(af, 1, IODD);
            continue;
        }
        copyfil(af, -1, IODD + SKIP);
    }
}

static void mcommand(void)
{
    init();
    if (getaf())
        noar();
    tf2 = mkstemp(tmp2nam);
    if (tf2 < 0) {
        fprintf(stderr, MSG("ar: cannot create third temporary file\n",
                            "ar: не могу создать третий временный файл\n"));
        done(1);
    }
    tf2nam = tmp2nam;
    while (!getdir()) {
        bamatch();
        if (match()) {
            mesg('m');
            copyfil(af, tf2, IODD + OODD + HEAD);
            continue;
        }
        mesg('c');
        copyfil(af, tf, IODD + OODD + HEAD);
    }
    install();
}

static void tcommand(void)
{
    if (getaf())
        noar();
    while (!getdir()) {
        if (namc == 0 || match()) {
            if (flg['v' - 'a'])
                longt();
            printf("%s\n", trim(file));
        }
        copyfil(af, -1, IODD + SKIP);
    }
}

static void qcommand(void)
{
    int i, f;

    // cppcheck-suppress duplicateExpression
    if (flg['a' - 'a'] || flg['b' - 'a']) {
        fprintf(stderr, MSG("ar: abi and q incompatible\n", "ar: abi нельзя с q\n"));
        done(1);
    }
    getqf();
    for (i = 0; signum[i]; i++)
        signal(signum[i], SIG_IGN);
    lseek(qf, 0l, SEEK_END);
    for (i = 0; i < namc; i++) {
        file = namv[i];
        if (file == 0)
            continue;
        namv[i] = 0;
        mesg('q');
        f = stats();
        if (f < 0) {
            fprintf(stderr, MSG("ar: cannot open %s\n", "ar: не могу открыть %s\n"), file);
            continue;
        }
        tf = qf;
        movefil(f);
        qf = tf;
    }
}

static void init(void)
{
    uword_t mbuf = ARMAG;

    tf = mkstemp(tmp0nam);
    if (tf < 0) {
        fprintf(stderr,
                MSG("ar: cannot create temporary file\n", "ar: не могу создать временный файл\n"));
        done(1);
    }
    tfnam = tmp0nam;
    if (!putint(tf, mbuf))
        wrerr();
}

static int getaf(void)
{
    uword_t mbuf;

    af = open(arnam, O_RDONLY);
    if (af < 0)
        return (1);
    if (!getint(af, &mbuf) || mbuf != ARMAG) {
        fprintf(stderr, MSG("ar: %s is not in archive format\n", "ar: %s не в формате архива\n"),
                arnam);
        done(1);
    }
    return (0);
}

static void getqf(void)
{
    uword_t mbuf;

    if ((qf = open(arnam, O_RDWR)) < 0) {
        if (!flg['c' - 'a'])
            fprintf(stderr, MSG("ar: creating %s\n", "ar: создание %s\n"), arnam);
        close(creat(arnam, 0666));
        if ((qf = open(arnam, O_RDWR)) < 0) {
            fprintf(stderr, MSG("ar: cannot create %s\n", "ar: не могу создать %s\n"), arnam);
            done(1);
        }
        mbuf = ARMAG;
        if (!putint(qf, mbuf))
            wrerr();
    } else if (!getint(qf, &mbuf) || mbuf != ARMAG) {
        fprintf(stderr, MSG("ar: %s is not in archive format\n", "ar: %s не в формате архива\n"),
                arnam);
        done(1);
    }
}

static void usage(void)
{
    printf(MSG("Usage: ar [%s][%s] archive file...\n", "Вызов: ar [%s][%s] архив файл...\n"), opt,
           man);
    done(1);
}

static void noar(void)
{
    fprintf(stderr, MSG("ar: %s not found\n", "ar: %s не существует\n"), arnam);
    done(1);
}

static void sigdone(int sig)
{
    (void)sig;
    done(100);
}

static void done(int c)
{
    if (tfnam)
        unlink(tfnam);
    if (tf1nam)
        unlink(tf1nam);
    if (tf2nam)
        unlink(tf2nam);
    exit_code = c;
    longjmp(done_env, 1);
}

static int notfound(void)
{
    int i, n;

    n = 0;
    for (i = 0; i < namc; i++)
        if (namv[i]) {
            fprintf(stderr, MSG("ar: %s not found\n", "ar: %s не найден\n"), namv[i]);
            n++;
        }
    return (n);
}

static int morefil(void)
{
    int i, n;

    n = 0;
    for (i = 0; i < namc; i++)
        if (namv[i])
            n++;
    return (n);
}

static void cleanup(void)
{
    int i, f;

    for (i = 0; i < namc; i++) {
        file = namv[i];
        if (file == 0)
            continue;
        namv[i] = 0;
        mesg('a');
        f = stats();
        if (f < 0) {
            fprintf(stderr, MSG("ar: cannot open %s\n", "ar: не могу открыть %s\n"), file);
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
    if (af < 0)
        if (!flg['c' - 'a'])
            fprintf(stderr, MSG("ar: creating %s\n", "ar: создание %s\n"), arnam);
    close(af);
    af = creat(arnam, 0666);
    if (af < 0) {
        fprintf(stderr, MSG("ar: cannot create %s\n", "ar: не могу создать %s\n"), arnam);
        done(1);
    }
    if (tfnam) {
        lseek(tf, 0l, SEEK_SET);
        while ((i = read(tf, buf, 512)) > 0)
            if (write(af, buf, i) != i)
                wrerr();
    }
    if (tf2nam) {
        lseek(tf2, 0l, SEEK_SET);
        while ((i = read(tf2, buf, 512)) > 0)
            if (write(af, buf, i) != i)
                wrerr();
    }
    if (tf1nam) {
        lseek(tf1, 0l, SEEK_SET);
        while ((i = read(tf1, buf, 512)) > 0)
            if (write(af, buf, i) != i)
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

    cp = trim(file);
    for (i = 0; i < (int)sizeof(arbuf.ar_name); i++)
        if ((arbuf.ar_name[i] = *cp))
            cp++;
    arbuf.ar_size = stbuf.st_size;
    arbuf.ar_date = stbuf.st_mtime;
    arbuf.ar_uid  = stbuf.st_uid;
    arbuf.ar_gid  = stbuf.st_gid;
    arbuf.ar_mode = stbuf.st_mode;
    copyfil(f, tf, OODD + HEAD);
    close(f);
}

static int stats(void)
{
    int f;

    f = open(file, O_RDONLY);
    if (f < 0)
        return (f);
    if (fstat(f, &stbuf) < 0) {
        close(f);
        return (-1);
    }
    return (f);
}

/*
 * copy next file
 * size given in arbuf
 *
 * Член архива дополняется нулями до целого слова БЭСМ-6 (W=6 байт), а в
 * заголовок записывается уже выровненный размер: ld шагает к следующему члену
 * по `ar_size + ARHDRSZ` без округления, так что ar_size обязан быть кратен W.
 */
static void copyfil(int fi, int fo, int flag)
{
    int pe;
    long size = arbuf.ar_size; // настоящее число байт данных
    int pad   = (int)((W - size % W) % W); // добивка до границы слова (0..W-1)

    if (flag & HEAD) {
        arbuf.ar_size = size + pad;
        if (!putarhdr(fo, &arbuf))
            wrerr();
    }
    pe = 0;
    while (size > 0) {
        int i, o;
        i = o = (size < 512) ? (int)size : 512;
        if (read(fi, buf, i) != i)
            pe++;
        if ((flag & SKIP) == 0)
            if (write(fo, buf, o) != o)
                wrerr();
        size -= 512;
    }
    if (pad) {
        if (flag & IODD)
            if (read(fi, buf, pad) != pad) // поглотить добивку из входа
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

    if (!getarhdr(af, &arbuf)) {
        if (tf1nam) {
            i   = tf;
            tf  = tf1;
            tf1 = i;
        }
        return (1);
    }
    for (i = 0; i < (int)sizeof(arbuf.ar_name); i++)
        name[i] = arbuf.ar_name[i];
    file = name;
    return (0);
}

static int match(void)
{
    int i;

    for (i = 0; i < namc; i++) {
        if (namv[i] == 0)
            continue;
        if (strcmp(trim(namv[i]), file) == 0) {
            file    = namv[i];
            namv[i] = 0;
            return (1);
        }
    }
    return (0);
}

static void bamatch(void)
{
    int f;

    switch (bastate) {
    case 1:
        if (strcmp(file, ponam) != 0)
            return;
        bastate = 2;
        // cppcheck-suppress duplicateExpression
        if (flg['a' - 'a'])
            return;
        /* fallthrough */

    case 2:
        bastate = 0;
        f       = mkstemp(tmp1nam);
        if (f < 0) {
            fprintf(stderr, MSG("ar: cannot create second temporary file\n",
                                "ar: не могу создать второй временный файл\n"));
            return;
        }
        tf1nam = tmp1nam;
        tf1    = tf;
        tf     = f;
    }
}

static void phserr(void)
{
    fprintf(stderr, MSG("ar: phase error on %s\n", "ar: ошибка фазы на %s\n"), file);
}

static void mesg(int c)
{
    if (flg['v' - 'a'])
        if (c != 'c' || flg['v' - 'a'] > 1)
            printf("%c - %s\n", c, file);
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
    printf("%3d/%1d", (int)arbuf.ar_uid, (int)arbuf.ar_gid);
    printf("%7ld", (long)arbuf.ar_size);
    t  = arbuf.ar_date;
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
    while (--n >= 0 && (arbuf.ar_mode & *ap++) == 0)
        ap++;
    putchar(*ap);
}

static void wrerr(void)
{
    perror("ar write error");
    done(1);
}
