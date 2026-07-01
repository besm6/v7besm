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

static char *command_letters = "mrxtdpq";
static char *option_letters  = "uvnbail";

static int caught_signals[] = { SIGHUP, SIGINT, SIGQUIT, 0 };

// All mutable per-run state, bundled so that reset_state() can clear it in one
// memset and repeated ar_run() calls do not depend on one another.
struct arstate {
    struct stat   filestat; // stat of the on-disk file being added
    struct ar_hdr hdr;      // current member header

    void (*command)(void); // selected command function

    // Option flags, one per accepted command-line letter.
    int opt_update;  // 'u': replace only if the input file is newer
    int opt_verbose; // 'v': verbose (counted; vv = extra detail)
    int opt_local;   // 'l': put temp files in cwd, not /tmp
    int opt_after;   // 'a': insert after position_name
    int opt_before;  // 'b': insert before position_name
    int opt_insert;  // 'i': synonym for -b
    int opt_create;  // 'c': suppress the "creating archive" notice

    char **names;        // named-file argv slice
    int    namecount;    // number of named files
    char  *archive_name; // archive path
    char  *position_name; // member named by -a/-b/-i

    // Temporary file name templates (mkstemp replaces the last six 'X' chars).
    char   tmpl_main[20];   // main
    char   tmpl_before[20]; // -b staging
    char   tmpl_move[20];   // m staging
    char  *tmp_name;        // active main temp name (NULL until created)
    char  *tmp1_name;       // active before-temp name
    char  *tmp2_name;       // active move-temp name

    char  *cur_file;        // current file/member name
    char   member_name[31]; // member-name buffer
    int    arfd;            // archive fd
    int    tmpfd;           // main temp fd
    int    tmp1fd;          // before-temp fd
    int    tmp2fd;          // move-temp fd
    int    qfd;             // quick-append fd
    int    insert_state;    // -a/-b insertion state (0/1/2)
    char   iobuf[512];      // I/O buffer

    // Single exit point: finish() unwinds via longjmp back into ar_run() so
    // that the engine can be invoked repeatedly within one process (from tests).
    jmp_buf done_env;
    int     exit_code;
};

static struct arstate ar;


static void finish(int c);
static void on_signal(int sig);
static void die_write_error(void);
static void die_usage(void);
static void die_no_archive(void);
static int open_archive(void);
static void open_archive_rw(void);
static int next_member(void);
static int match_member(void);
static void handle_position(void);
static int open_input(void);
static int report_missing(void);
static int count_pending(void);
static char *basename_of(char *s);
static void print_long_entry(void);
static void print_perm_bits(void);
static void print_perm_char(const int *pairp);
static void copy_member(int fi, int fo, int flag);
static void write_member(int f);
static void start_temp_archive(void);
static void append_new_files(void);
static void commit_archive(void);
static void log_action(int c);
static void phase_error(void);
static void set_command(void (*fun)(void));
static void cmd_replace(void);
static void cmd_delete(void);
static void cmd_extract(void);
static void cmd_list(void);
static void cmd_print(void);
static void cmd_move(void);
static void cmd_quick(void);

// Reset global state so that repeated ar_run() calls do not depend on
// one another.
static void reset_state(void)
{
    memset(&ar, 0, sizeof(ar));
    strcpy(ar.tmpl_main, "/tmp/ar0XXXXXX");
    strcpy(ar.tmpl_before, "/tmp/ar1XXXXXX");
    strcpy(ar.tmpl_move, "/tmp/ar2XXXXXX");
}

int ar_run(int argc, char **argv)
{
    int i;
    char *cp;

    reset_state();
    ar.exit_code = 0;
    if (setjmp(ar.done_env))
        return ar.exit_code;

    for (i = 0; caught_signals[i]; i++)
        if (signal(caught_signals[i], SIG_IGN) != SIG_IGN)
            signal(caught_signals[i], on_signal);
    if (argc < 3)
        die_usage();
    for (cp = argv[1]; *cp; cp++)
        switch (*cp) {
        case 'u':
            ar.opt_update++;
            continue;

        case 'v':
            ar.opt_verbose++;
            continue;

        case 'l':
            ar.opt_local++;
            continue;

        case 'a':
            ar.opt_after++;
            continue;

        case 'b':
            ar.opt_before++;
            continue;

        case 'i':
            ar.opt_insert++;
            continue;

        case 'c':
            ar.opt_create++;
            continue;

        case 'n':
            // accepted for compatibility, ignored
            continue;

        case 'r':
            set_command(cmd_replace);
            continue;

        case 'd':
            set_command(cmd_delete);
            continue;

        case 'x':
            set_command(cmd_extract);
            continue;

        case 't':
            set_command(cmd_list);
            continue;

        case 'p':
            set_command(cmd_print);
            continue;

        case 'm':
            set_command(cmd_move);
            continue;

        case 'q':
            set_command(cmd_quick);
            continue;

        default:
            fprintf(stderr, "ar: unknown flag `%c'\n", *cp);
            finish(1);
        }
    if (ar.opt_local) {
        strcpy(ar.tmpl_main, "ar0XXXXXX");
        strcpy(ar.tmpl_before, "ar1XXXXXX");
        strcpy(ar.tmpl_move, "ar2XXXXXX");
    }
    if (ar.opt_insert)
        ar.opt_before++;
    if (ar.opt_after || ar.opt_before) {
        ar.insert_state  = 1;
        ar.position_name = basename_of(argv[2]);
        argv++;
        argc--;
        if (argc < 3)
            die_usage();
    }
    ar.archive_name = argv[2];
    ar.names        = argv + 3;
    ar.namecount    = argc - 3;
    if (ar.command == 0) {
        if (ar.opt_update == 0) {
            fprintf(stderr, "ar: must be one of [%s]\n",
                    command_letters);
            finish(1);
        }
        set_command(cmd_replace);
    }
    (*ar.command)();
    finish(report_missing());
    return ar.exit_code;
}

static void set_command(void (*fun)(void))
{
    if (ar.command != 0) {
        fprintf(stderr, "ar: only one of [%s] allowed\n",
                command_letters);
        finish(1);
    }
    ar.command = fun;
}

static void cmd_replace(void)
{
    int f;

    start_temp_archive();
    open_archive();
    while (!next_member()) {
        handle_position();
        if (ar.namecount == 0 || match_member()) {
            f = open_input();
            if (f < 0) {
                if (ar.namecount)
                    fprintf(stderr, "ar: cannot open %s\n", ar.cur_file);
                goto cp;
            }
            if (ar.opt_update)
                if (ar.filestat.st_mtime <= ar.hdr.ar_date) {
                    close(f);
                    goto cp;
                }
            log_action('r');
            copy_member(ar.arfd, -1, IODD + SKIP);
            write_member(f);
            continue;
        }
    cp:
        log_action('c');
        copy_member(ar.arfd, ar.tmpfd, IODD + OODD + HEAD);
    }
    append_new_files();
}

static void cmd_delete(void)
{
    start_temp_archive();
    if (open_archive())
        die_no_archive();
    while (!next_member()) {
        if (match_member()) {
            log_action('d');
            copy_member(ar.arfd, -1, IODD + SKIP);
            continue;
        }
        log_action('c');
        copy_member(ar.arfd, ar.tmpfd, IODD + OODD + HEAD);
    }
    commit_archive();
}

static void cmd_extract(void)
{
    int f;

    if (open_archive())
        die_no_archive();
    while (!next_member()) {
        if (ar.namecount == 0 || match_member()) {
            f = creat(ar.cur_file, ar.hdr.ar_mode & 0777);
            if (f < 0) {
                fprintf(stderr, "ar: cannot create %s\n", ar.cur_file);
                goto sk;
            }
            log_action('x');
            copy_member(ar.arfd, f, IODD);
            close(f);
            continue;
        }
    sk:
        log_action('c');
        copy_member(ar.arfd, -1, IODD + SKIP);
        if (ar.namecount > 0 && !count_pending())
            finish(0);
    }
}

static void cmd_print(void)
{
    if (open_archive())
        die_no_archive();
    while (!next_member()) {
        if (ar.namecount == 0 || match_member()) {
            if (ar.opt_verbose) {
                printf("\n<%s>\n\n", ar.cur_file);
                fflush(stdout);
            }
            copy_member(ar.arfd, 1, IODD);
            continue;
        }
        copy_member(ar.arfd, -1, IODD + SKIP);
    }
}

static void cmd_move(void)
{
    start_temp_archive();
    if (open_archive())
        die_no_archive();
    ar.tmp2fd = mkstemp(ar.tmpl_move);
    if (ar.tmp2fd < 0) {
        fprintf(stderr, "ar: cannot create third temporary file\n");
        finish(1);
    }
    ar.tmp2_name = ar.tmpl_move;
    while (!next_member()) {
        handle_position();
        if (match_member()) {
            log_action('m');
            copy_member(ar.arfd, ar.tmp2fd, IODD + OODD + HEAD);
            continue;
        }
        log_action('c');
        copy_member(ar.arfd, ar.tmpfd, IODD + OODD + HEAD);
    }
    commit_archive();
}

static void cmd_list(void)
{
    if (open_archive())
        die_no_archive();
    while (!next_member()) {
        if (ar.namecount == 0 || match_member()) {
            if (ar.opt_verbose)
                print_long_entry();
            printf("%s\n", basename_of(ar.cur_file));
        }
        copy_member(ar.arfd, -1, IODD + SKIP);
    }
}

static void cmd_quick(void)
{
    int i, f;

    if (ar.opt_after || ar.opt_before) {
        fprintf(stderr, "ar: abi and q incompatible\n");
        finish(1);
    }
    open_archive_rw();
    for (i = 0; caught_signals[i]; i++)
        signal(caught_signals[i], SIG_IGN);
    lseek(ar.qfd, 0l, SEEK_END);
    for (i = 0; i < ar.namecount; i++) {
        ar.cur_file = ar.names[i];
        if (ar.cur_file == 0)
            continue;
        ar.names[i] = 0;
        log_action('q');
        f = open_input();
        if (f < 0) {
            fprintf(stderr, "ar: cannot open %s\n", ar.cur_file);
            continue;
        }
        ar.tmpfd = ar.qfd;
        write_member(f);
        ar.qfd = ar.tmpfd;
    }
}

static void start_temp_archive(void)
{
    uword_t mbuf = ARMAG;

    ar.tmpfd = mkstemp(ar.tmpl_main);
    if (ar.tmpfd < 0) {
        fprintf(stderr,
                "ar: cannot create temporary file\n");
        finish(1);
    }
    ar.tmp_name = ar.tmpl_main;
    if (!putint(ar.tmpfd, mbuf))
        die_write_error();
}

static int open_archive(void)
{
    uword_t mbuf;

    ar.arfd = open(ar.archive_name, O_RDONLY);
    if (ar.arfd < 0)
        return (1);
    if (!getint(ar.arfd, &mbuf) || mbuf != ARMAG) {
        fprintf(stderr, "ar: %s is not in archive format\n",
                ar.archive_name);
        finish(1);
    }
    return (0);
}

static void open_archive_rw(void)
{
    uword_t mbuf;

    if ((ar.qfd = open(ar.archive_name, O_RDWR)) < 0) {
        if (!ar.opt_create)
            fprintf(stderr, "ar: creating %s\n", ar.archive_name);
        close(creat(ar.archive_name, 0666));
        if ((ar.qfd = open(ar.archive_name, O_RDWR)) < 0) {
            fprintf(stderr, "ar: cannot create %s\n", ar.archive_name);
            finish(1);
        }
        mbuf = ARMAG;
        if (!putint(ar.qfd, mbuf))
            die_write_error();
    } else if (!getint(ar.qfd, &mbuf) || mbuf != ARMAG) {
        fprintf(stderr, "ar: %s is not in archive format\n",
                ar.archive_name);
        finish(1);
    }
}

static void die_usage(void)
{
    printf("Usage: ar [%s][%s] archive file...\n", option_letters,
           command_letters);
    finish(1);
}

static void die_no_archive(void)
{
    fprintf(stderr, "ar: %s not found\n", ar.archive_name);
    finish(1);
}

static void on_signal(int sig)
{
    (void)sig;
    finish(100);
}

static void finish(int c)
{
    if (ar.tmp_name)
        unlink(ar.tmp_name);
    if (ar.tmp1_name)
        unlink(ar.tmp1_name);
    if (ar.tmp2_name)
        unlink(ar.tmp2_name);
    ar.exit_code = c;
    longjmp(ar.done_env, 1);
}

static int report_missing(void)
{
    int i, n;

    n = 0;
    for (i = 0; i < ar.namecount; i++)
        if (ar.names[i]) {
            fprintf(stderr, "ar: %s not found\n", ar.names[i]);
            n++;
        }
    return (n);
}

static int count_pending(void)
{
    int i, n;

    n = 0;
    for (i = 0; i < ar.namecount; i++)
        if (ar.names[i])
            n++;
    return (n);
}

static void append_new_files(void)
{
    int i, f;

    for (i = 0; i < ar.namecount; i++) {
        ar.cur_file = ar.names[i];
        if (ar.cur_file == 0)
            continue;
        ar.names[i] = 0;
        log_action('a');
        f = open_input();
        if (f < 0) {
            fprintf(stderr, "ar: cannot open %s\n", ar.cur_file);
            continue;
        }
        write_member(f);
    }
    commit_archive();
}

static void commit_archive(void)
{
    int i;

    for (i = 0; caught_signals[i]; i++)
        signal(caught_signals[i], SIG_IGN);
    if (ar.arfd < 0)
        if (!ar.opt_create)
            fprintf(stderr, "ar: creating %s\n", ar.archive_name);
    close(ar.arfd);
    ar.arfd = creat(ar.archive_name, 0666);
    if (ar.arfd < 0) {
        fprintf(stderr, "ar: cannot create %s\n", ar.archive_name);
        finish(1);
    }
    if (ar.tmp_name) {
        lseek(ar.tmpfd, 0l, SEEK_SET);
        while ((i = read(ar.tmpfd, ar.iobuf, 512)) > 0)
            if (write(ar.arfd, ar.iobuf, i) != i)
                die_write_error();
    }
    if (ar.tmp2_name) {
        lseek(ar.tmp2fd, 0l, SEEK_SET);
        while ((i = read(ar.tmp2fd, ar.iobuf, 512)) > 0)
            if (write(ar.arfd, ar.iobuf, i) != i)
                die_write_error();
    }
    if (ar.tmp1_name) {
        lseek(ar.tmp1fd, 0l, SEEK_SET);
        while ((i = read(ar.tmp1fd, ar.iobuf, 512)) > 0)
            if (write(ar.arfd, ar.iobuf, i) != i)
                die_write_error();
    }
}

/*
 * insert the file 'cur_file'
 * into the temporary file
 */
static void write_member(int f)
{
    const char *cp;
    int i;

    cp = basename_of(ar.cur_file);
    for (i = 0; i < (int)sizeof(ar.hdr.ar_name); i++)
        if ((ar.hdr.ar_name[i] = *cp))
            cp++;
    ar.hdr.ar_size = ar.filestat.st_size;
    ar.hdr.ar_date = ar.filestat.st_mtime;
    ar.hdr.ar_uid  = ar.filestat.st_uid;
    ar.hdr.ar_gid  = ar.filestat.st_gid;
    ar.hdr.ar_mode = ar.filestat.st_mode;
    copy_member(f, ar.tmpfd, OODD + HEAD);
    close(f);
}

static int open_input(void)
{
    int f;

    f = open(ar.cur_file, O_RDONLY);
    if (f < 0)
        return (f);
    if (fstat(f, &ar.filestat) < 0) {
        close(f);
        return (-1);
    }
    return (f);
}

/*
 * copy next file
 * size given in hdr
 *
 * An archive member is zero-padded to a whole BESM-6 word (W=6 bytes), and the
 * already-aligned size is written into the header: ld steps to the next member
 * by `ar_size + ARHDRSZ` without rounding, so ar_size must be a multiple of W.
 */
static void copy_member(int fi, int fo, int flag)
{
    int pe;
    long size = ar.hdr.ar_size; // actual number of data bytes
    int pad   = (int)((W - size % W) % W); // padding to the word boundary (0..W-1)

    if (flag & HEAD) {
        ar.hdr.ar_size = size + pad;
        if (!putarhdr(fo, &ar.hdr))
            die_write_error();
    }
    pe = 0;
    while (size > 0) {
        int i, o;
        i = o = (size < 512) ? (int)size : 512;
        if (read(fi, ar.iobuf, i) != i)
            pe++;
        if ((flag & SKIP) == 0)
            if (write(fo, ar.iobuf, o) != o)
                die_write_error();
        size -= 512;
    }
    if (pad) {
        if (flag & IODD)
            if (read(fi, ar.iobuf, pad) != pad) // consume the padding from the input
                pe++;
        if ((flag & OODD) && (flag & SKIP) == 0) {
            char zero[W];
            memset(zero, 0, sizeof(zero));
            if (write(fo, zero, pad) != pad)
                die_write_error();
        }
    }
    if (pe)
        phase_error();
}

static int next_member(void)
{
    int i;

    if (!getarhdr(ar.arfd, &ar.hdr)) {
        if (ar.tmp1_name) {
            i         = ar.tmpfd;
            ar.tmpfd  = ar.tmp1fd;
            ar.tmp1fd = i;
        }
        return (1);
    }
    for (i = 0; i < (int)sizeof(ar.hdr.ar_name); i++)
        ar.member_name[i] = ar.hdr.ar_name[i];
    ar.cur_file = ar.member_name;
    return (0);
}

static int match_member(void)
{
    int i;

    for (i = 0; i < ar.namecount; i++) {
        if (ar.names[i] == 0)
            continue;
        if (strcmp(basename_of(ar.names[i]), ar.cur_file) == 0) {
            ar.cur_file = ar.names[i];
            ar.names[i] = 0;
            return (1);
        }
    }
    return (0);
}

static void handle_position(void)
{
    int f;

    switch (ar.insert_state) {
    case 1:
        if (strcmp(ar.cur_file, ar.position_name) != 0)
            return;
        ar.insert_state = 2;
        if (ar.opt_after)
            return;
        /* fallthrough */

    case 2:
        ar.insert_state = 0;
        f               = mkstemp(ar.tmpl_before);
        if (f < 0) {
            fprintf(stderr, "ar: cannot create second temporary file\n");
            return;
        }
        ar.tmp1_name = ar.tmpl_before;
        ar.tmp1fd    = ar.tmpfd;
        ar.tmpfd     = f;
    }
}

static void phase_error(void)
{
    fprintf(stderr, "ar: phase error on %s\n", ar.cur_file);
}

static void log_action(int c)
{
    if (ar.opt_verbose)
        if (c != 'c' || ar.opt_verbose > 1)
            printf("%c - %s\n", c, ar.cur_file);
}

static char *basename_of(char *s)
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

static void print_long_entry(void)
{
    const char *cp;
    time_t t;

    print_perm_bits();
    printf("%3d/%1d", (int)ar.hdr.ar_uid, (int)ar.hdr.ar_gid);
    printf("%7ld", (long)ar.hdr.ar_size);
    t  = ar.hdr.ar_date;
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

static void print_perm_bits(void)
{
    int **mp;

    for (mp = &m[0]; mp < &m[9];)
        print_perm_char(*mp++);
}

static void print_perm_char(const int *pairp)
{
    int n;
    const int *ap;

    ap = pairp;
    n  = *ap++;
    while (--n >= 0 && (ar.hdr.ar_mode & *ap++) == 0)
        ap++;
    putchar(*ap);
}

static void die_write_error(void)
{
    perror("ar write error");
    finish(1);
}
