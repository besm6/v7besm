/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// /etc/cron -- run the commands of /usr/lib/crontab at the times that file names.  Task
// C22; README.md is the account.
//
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define LISTS  512 // the compiled crontab is allocated, and grown, this many bytes at a time
#define MAXVAL 100 // a field value is 0..99

// The tag a compiled field carries.
#define EXACT 100
#define ANY   101
#define LIST  102
#define RANGE 103
#define EOS   104

_Static_assert(EXACT >= MAXVAL && EOS <= 255, "a field value must not collide with a tag");

static char crontab[] = "/usr/lib/crontab";
static time_t itime; // the minute being served
static int nomatch;  // v7's `flag': set by cmp() when a field does not match
static FILE *cf;     // the crontab while init() reads it

// The compiled crontab.  The cursor is an OFFSET because realloc() may move the block.
static char *list;
static int listsize; // bytes allocated
static int listlen;  // bytes in use
static int listerr;  // sticky: a realloc failed, and `list' went with it

static void put(int c);
static int cmp(int i, int v);
static void slp(void);
static void ex(char *s);
static int init(void);
static int number(int c);

int main(void)
{
    time_t filetime = 0;
    int i;

    // No setuid(1), and v7 had one: /usr/lib/atrun needs suser().  README.md.
    if (fork())
        exit(0); // the parent goes, so /etc/rc does not wait for what never returns

    chdir("/");

    // /dev/null on all three, not v7's read-only `/' and not update's bare closes: a job
    // inheriting a free descriptor would get it back from its own first open(2).
    for (i = 0; i < NOFILE; i++)
        close(i);
    open("/dev/null", O_RDWR);
    dup(0);
    dup(0);

    signal(SIGHUP, SIG_IGN); // and these survive exec, so every job inherits them
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);

    time(&itime);
    itime -= localtime(&itime)->tm_sec; // truncate to the minute

    for (;; itime += 60, slp()) {
        struct stat cstat;
        struct tm *loct;

        if (stat(crontab, &cstat) == -1)
            continue;
        if (cstat.st_mtime != filetime) { // v7 wrote `>', which misses a restored file
            if (init() < 0) {
                filetime = 0; // try again next minute, and do not walk what init() freed
                continue;
            }
            filetime = cstat.st_mtime;
        }
        if (list == NULL)
            continue;

        loct = localtime(&itime);
        loct->tm_mon++; // 1-12 for month
        for (i = 0; list[i] != EOS;) {
            nomatch = 0;
            i       = cmp(i, loct->tm_min);
            i       = cmp(i, loct->tm_hour);
            i       = cmp(i, loct->tm_mday);
            i       = cmp(i, loct->tm_mon);
            i       = cmp(i, loct->tm_wday); // 0-6, Sunday is 0
            if (nomatch == 0) {
                slp();
                ex(list + i);
            }
            while (list[i++] != 0)
                ;
        }
    }
    // NOTREACHED
}

// Append one byte, growing the block when it fills.  v7 checked its headroom once a line and
// then stored without bound.  Sticky failure: there is nobody to report to.
static void put(int c)
{
    char *nl;

    if (listerr)
        return;
    if (listlen >= listsize) {
        nl = realloc(list, listsize + LISTS); // realloc(NULL, n) is malloc(n) here
        if (nl == NULL) {
            listerr  = 1; // a failed realloc has already freed the old block
            list     = NULL;
            listsize = 0;
            listlen  = 0;
            return;
        }
        list = nl;
        listsize += LISTS;
    }
    list[listlen++] = c;
}

// Match one compiled field, setting `nomatch' when it does not, and return the next offset.
static int cmp(int i, int v)
{
    int c = list[i++];

    switch (c) {
    case EXACT:
        if (list[i++] != v)
            nomatch++;
        return i;

    case ANY:
        return i;

    case LIST:
        // Terminates: a value is 0..99 and init() leaves no half-built record reachable.
        while (list[i] != LIST) {
            if (list[i++] == v) {
                while (list[i++] != LIST)
                    ;
                return i;
            }
        }
        nomatch++;
        return i + 1;

    case RANGE:
        if (list[i] > v || list[i + 1] < v)
            nomatch++;
        return i + 2;
    }
    nomatch++; // init() emits no bare value; kept so a damaged list costs a job, not a walk
    return i;
}

// Sleep until itime, the minute being served.
static void slp(void)
{
    time_t t;
    int i;

    time(&t);
    i = itime - t;
    if (i > 0)
        sleep(i);
}

// Hand one command to the shell.  Two forks: the middle child leaves at once, so the job is
// orphaned onto init and a minute-long loop never waits on an hour-long job.
static void ex(char *s)
{
    if (fork()) {
        wait((int *)0);
        return;
    }
    if (fork())
        _exit(0); // _exit: this child inherited the daemon's stdio buffers

    // 0, 1 and 2 are /dev/null already, so v7's freopen("/") is gone with the hole it plugged.
    execl("/bin/sh", "sh", "-c", s, (char *)0);
    _exit(1);
}

// Compile the crontab: five tagged fields, the command text, a newline and a NUL per record,
// two EOS at the end.  -1 when nothing usable came of it.  A line that does not parse is
// dropped in silence, and so is a last line with no newline -- both v7's.
static int init(void)
{
    int i, c, n, linebase;

    if ((cf = fopen(crontab, "r")) == NULL)
        return -1;
    listlen = 0;
    listerr = 0;

    for (;;) {
        linebase = listlen; // rewind mark for a bad line; an offset, so a grow cannot stale it
        for (i = 0;; i++) {
            do
                c = getc(cf);
            while (c == ' ' || c == '\t');
            if (c == EOF || c == '\n')
                goto ignore;
            if (i == 5)
                break; // c is the first byte of the command, which may be a `#'
            if (c == '#')
                goto ignore;
            if (c == '*') {
                put(ANY);
                continue;
            }
            if ((n = number(c)) < 0)
                goto ignore;
            c = getc(cf);
            if (c == ',')
                goto mlist;
            if (c == '-')
                goto mrange;
            if (c != '\t' && c != ' ')
                goto ignore;
            put(EXACT);
            put(n);
            continue;

        mlist:
            put(LIST);
            put(n);
            do {
                if ((n = number(getc(cf))) < 0)
                    goto ignore;
                put(n);
                c = getc(cf);
            } while (c == ',');
            if (c != '\t' && c != ' ')
                goto ignore;
            put(LIST);
            continue;

        mrange:
            put(RANGE);
            put(n);
            if ((n = number(getc(cf))) < 0)
                goto ignore;
            c = getc(cf);
            if (c != '\t' && c != ' ')
                goto ignore;
            put(n);
        }
        while (c != '\n') { // the command field
            if (c == EOF)
                goto ignore;
            if (c == '%')
                c = '\n'; // cron.8's percent
            put(c);
            c = getc(cf);
        }
        put('\n');
        put(0);
        continue;

    ignore:
        listlen = linebase;
        while (c != '\n') {
            if (c == EOF) {
                put(EOS);
                put(EOS);
                fclose(cf);
                return listerr ? -1 : 0;
            }
            c = getc(cf);
        }
    }
}

// One field value, or -1 for anything that cannot be one.  Not isdigit(): the argument is
// getc()'s result, and <ctype.h> here is 129 entries.  ../m4/README.md.
static int number(int c)
{
    int n = 0, nd = 0;

    while (c >= '0' && c <= '9') {
        n = n * 10 + c - '0';
        if (n >= MAXVAL)
            return -1;
        nd++;
        c = getc(cf);
    }
    ungetc(c, cf);
    if (nd == 0)
        return -1; // v7 returned 0, so `1,,2' compiled as if a 0 had been written
    return n;
}
