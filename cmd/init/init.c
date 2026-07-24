/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// /etc/init -- process 1: the single-user shell, /etc/rc, and a getty per line.
//
// The v7 program, unchanged in what it does; the changes are the ones C11 forces on a
// source written for a compiler that defaulted everything to `int'.  b6parse has no
// implicit int, no K&R parameter lists and no untyped `register i;', so:
//
//   - every function has a prototype and an explicit return type, and the ones only
//     this file calls are `static';
//   - the K&R definitions (`term(p) register struct tab *p;') became prototypes, and
//     the bare `register' declarations became plain ones -- the register keyword bought
//     nothing here, the back end allocates its own;
//   - merge() and reset() are HANDLERS, so C11 gives them the `void (*)(int)' shape
//     <signal.h> declares.  v7 could hand signal() a niladic function; C11 cannot, so
//     merge() takes the signal it ignores and main() calls it as merge(0);
//   - the flag arguments are spelled (O_RDWR, SEEK_END) rather than written as the
//     small integers v7 used, now that <fcntl.h> and <unistd.h> name them.
//
// The one change of substance is in <utmp.h>, not here: ut_time is a time_t, where v7
// wrote `long'.  Same word, but time() takes a `time_t *', so the `long' would not
// compile.
//
// WHAT IT DOES ON THIS MACHINE TODAY.  With no /etc/ttys on the root image, merge()
// returns as soon as the open fails and multiple() falls straight through -- so this
// init is exactly the single-user loop the port needs: shutdown, a shell on
// /dev/console, /etc/rc, and around again when the shell exits.  getty and the
// multi-user half wait on a terminal driver (kernel/TODO.md, task 29).  Until /bin/sh
// exists on the image, single()'s execl fails and the child exits at once, so the loop
// spins: the disk manifest keeps naming the task-23 coninit until the shell arrives.
//
#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <utmp.h>

#define TABSIZ 100

// v7's two loop macros, each an ENTIRE for() control clause -- semicolons included --
// which is why clang-format has to be told to leave them alone: it reads the semicolons
// as statement ends and breaks each macro across three lines.
// clang-format off
#define ALL  p = &itab[0]; p < &itab[TABSIZ]; p++
#define EVER ;;
// clang-format on

char shell[] = "/bin/sh";
char getty[] = "/etc/getty";
char minus[] = "-";
char runc[]  = "/etc/rc";
char ifile[] = "/etc/ttys";
char utmp[]  = "/etc/utmp";
char wtmpf[] = "/usr/adm/wtmp";
char ctty[]  = "/dev/console";
char dev[]   = "/dev/";

struct utmp wtmp;
struct {
    char line[8];
    char comn;
    char flag;
} line;
struct tab {
    char line[8];
    char comn;
    int pid;
} itab[TABSIZ];

int fi;
char tty[20];
jmp_buf sjbuf;

static void shutdown(void);
static void single(void);
static void runcom(void);
static void merge(int sig);
static void multiple(void);
static void term(struct tab *p);
static int rline(void);
static void maktty(char *lin);
static int get(void);
static void dfork(struct tab *p);
static void rmut(struct tab *p);
static void reset(int sig);

int main(void)
{
    setjmp(sjbuf);
    signal(SIGHUP, reset);
    for (EVER) {
        shutdown();
        single();
        runcom();
        merge(0);
        multiple();
    }
}

static void shutdown(void)
{
    int i;
    struct tab *p;

    signal(SIGINT, SIG_IGN);
    for (ALL)
        term(p);
    signal(SIGALRM, reset);
    alarm(60);
    for (i = 0; i < 5; i++)
        kill(-1, SIGKILL);
    while (wait((int *)0) != -1)
        ;
    alarm(0);
    signal(SIGALRM, SIG_DFL);
    for (i = 0; i < 10; i++)
        close(i);
}

static void single(void)
{
    int pid;

    pid = fork();
    if (pid == 0) {
        /*
                alarm(300);
        */
        signal(SIGHUP, SIG_DFL);
        signal(SIGINT, SIG_DFL);
        signal(SIGALRM, SIG_DFL);
        open(ctty, O_RDWR);
        dup(0);
        dup(0);
        execl(shell, minus, (char *)0);
        exit(0);
    }
    while (wait((int *)0) != pid)
        ;
}

static void runcom(void)
{
    int pid;

    pid = fork();
    if (pid == 0) {
        open("/", O_RDONLY);
        dup(0);
        dup(0);
        execl(shell, shell, runc, (char *)0);
        exit(0);
    }
    while (wait((int *)0) != pid)
        ;
}

static void multiple(void)
{
    struct tab *p;
    int pid;

    for (EVER) {
        pid = wait((int *)0);
        if (pid == -1)
            return;
        for (ALL)
            if (p->pid == pid || p->pid == -1) {
                rmut(p);
                dfork(p);
            }
    }
}

static void term(struct tab *p)
{
    if (p->pid != 0) {
        rmut(p);
        kill(p->pid, SIGKILL);
    }
    p->pid     = 0;
    p->line[0] = 0;
}

static int rline(void)
{
    int c, i;

    c = get();
    if (c < 0)
        return (0);
    if (c == 0)
        goto bad;
    line.flag = c;
    c         = get();
    if (c <= 0)
        goto bad;
    line.comn = c;
    for (i = 0; i < 8; i++)
        line.line[i] = 0;
    for (i = 0; i < 7; i++) {
        c = get();
        if (c <= 0)
            break;
        line.line[i] = c;
    }
    while (c > 0)
        c = get();
    maktty(line.line);
    if (access(tty, 06) < 0)
        goto bad;
    return (1);

bad:
    line.flag = '0';
    return (1);
}

static void maktty(char *lin)
{
    int i, j;

    for (i = 0; dev[i]; i++)
        tty[i] = dev[i];
    for (j = 0; lin[j]; j++) {
        tty[i] = lin[j];
        i++;
    }
    tty[i] = 0;
}

static int get(void)
{
    char b;

    if (read(fi, &b, 1) != 1)
        return (-1);
    if (b == '\n')
        return (0);
    return (b);
}

// Also the SIGINT handler, which is why it takes an argument it does not use: an
// interrupt during the multi-user pass re-reads /etc/ttys, which is v7's way of
// asking init to notice a changed line table.
static void merge(int sig)
{
    struct tab *p, *q;
    int i;

    (void)sig;
    close(creat(utmp, 0644));
    signal(SIGINT, merge);
    fi = open(ifile, O_RDONLY);
    if (fi < 0)
        return;
    q = &itab[0];
    while (rline()) {
        if (line.flag == '0')
            continue;
        for (ALL) {
            if (p->line[0] != 0)
                for (i = 0; i < 8; i++)
                    if (p->line[i] != line.line[i])
                        goto contin;
            if (p >= q) {
                i      = p->pid;
                p->pid = q->pid;
                q->pid = i;
                for (i = 0; i < 8; i++)
                    p->line[i] = q->line[i];
                p->comn = q->comn;
                for (i = 0; i < 8; i++)
                    q->line[i] = line.line[i];
                q->comn = line.comn;
                q++;
            }
            break;
        contin:;
        }
    }
    close(fi);
    for (; q < &itab[TABSIZ]; q++)
        term(q);
    for (ALL)
        if (p->line[0] != 0 && p->pid == 0)
            dfork(p);
}

static void dfork(struct tab *p)
{
    int pid;

    pid = fork();
    if (pid == 0) {
        signal(SIGHUP, SIG_DFL);
        signal(SIGINT, SIG_DFL);
        maktty(p->line);
        chown(tty, 0, 0);
        chmod(tty, 0622);
        open(tty, O_RDWR);
        dup(0);
        dup(0);
        tty[0] = p->comn;
        tty[1] = 0;
        execl(getty, minus, tty, (char *)0);
        exit(0);
    }
    p->pid = pid;
}

static void rmut(struct tab *p)
{
    int i, f;

    f = open(utmp, O_RDWR);
    if (f >= 0) {
        while (read(f, (char *)&wtmp, sizeof(wtmp)) == sizeof(wtmp)) {
            for (i = 0; i < 8; i++)
                if (wtmp.ut_line[i] != p->line[i])
                    goto contin;
            lseek(f, -(off_t)sizeof(wtmp), SEEK_CUR);
            for (i = 0; i < 8; i++)
                wtmp.ut_name[i] = 0;
            time(&wtmp.ut_time);
            write(f, (char *)&wtmp, sizeof(wtmp));
        contin:;
        }
        close(f);
    }
    f = open(wtmpf, O_WRONLY);
    if (f >= 0) {
        for (i = 0; i < 8; i++) {
            wtmp.ut_name[i] = 0;
            wtmp.ut_line[i] = p->line[i];
        }
        time(&wtmp.ut_time);
        lseek(f, 0, SEEK_END);
        write(f, (char *)&wtmp, sizeof(wtmp));
        close(f);
    }
}

// The SIGHUP handler: back to the top of main()'s loop, which is what makes a hangup on
// the console take the system back to single-user.
static void reset(int sig)
{
    (void)sig;
    longjmp(sjbuf, 1);
}
