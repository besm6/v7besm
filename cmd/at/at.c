/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// at time [ day ] [ file ] -- spool a script for /usr/lib/atrun to run later.  Task C21;
// README.md beside this file is the account, notably of the year the spool name carries.
//
#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define HOUR    100
#define HALFDAY (12 * HOUR)
#define DAY     (24 * HOUR)
#define THISDAY "/usr/spool/at"

// Declared by no header, as in cmd/date: years since 1900, full Gregorian rule.
int dysize(int y);

// crt0's (lib/libc/csu/crt0.s).  A definition here would take its own storage and every
// job would be written with no environment at all.
extern char **environ;

static char *days[] = {
    "sunday", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday",
};

// v7 held these in one array of { char *mname; int mlen; }.  b6lower cannot initialize a
// char * inside a struct initializer (../README.md), so they are two arrays.
#define NMONTHS 12

static char *months[NMONTHS] = {
    "january", "february", "march",     "april",   "may",      "june",
    "july",    "august",   "september", "october", "november", "december",
};

static int mlen[NMONTHS] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
};

static char fname[100];
static int attime; // requested time in grains (hhmm as a plain int); v7 called it utime,
                   // which is a libc function here
static int now;    // what o'clock it is
static int uday;   // day of year to be done
static int uyear;  // year, in full: the spool name carries all four digits
static int today;  // day of year today
static FILE *file;
static FILE *ifile;

static void makeutime(char *pp);
static int makeuday(int argc, char **argv);
static char *prefix(char *begin, char *full);
static void filename(char *dir, int y, int d, int t);
static void onintr(int sig);

int main(int argc, char **argv)
{
    int c;
    char pwbuf[100];
    FILE *pwfil;
    int larg;

    // argv[1] is the user's time: e.g., 3AM
    // argv[2] is a month name or day of week
    // argv[3] is day of month or 'week'
    // another argument might be an input file
    if (argc < 2) {
        fprintf(stderr, "at: arg count\n");
        exit(1);
    }
    makeutime(argv[1]);
    larg = makeuday(argc, argv) + 1;
    if (uday == today && larg <= 2 && attime <= now)
        uday++;
    c = dysize(uyear - 1900);
    if (uday >= c) {
        uday -= c;
        uyear++;
    }
    filename(THISDAY, uyear, uday, attime);
    ifile = stdin;
    if (argc > larg)
        ifile = fopen(argv[larg], "r");
    if (ifile == NULL) {
        fprintf(stderr, "at: cannot open input: %s\n", argv[larg]);
        exit(1);
    }
    if (signal(SIGINT, SIG_IGN) != SIG_IGN)
        signal(SIGINT, onintr);
    file = fopen(fname, "a");
    if (file == NULL) {
        fprintf(stderr, "at: cannot open memo file\n");
        exit(1);
    }
    chmod(fname, 0644);
    if ((pwfil = popen("pwd", "r")) == NULL) {
        fprintf(stderr, "at: can't execute pwd\n");
        exit(1);
    }
    pwbuf[0] = '\0';
    fgets(pwbuf, sizeof(pwbuf), pwfil);
    pclose(pwfil);
    fprintf(file, "cd %s", pwbuf);
    if (environ) {
        char **ep = environ;
        while (*ep)
            fprintf(file, "%s\n", *ep++);
    }
    while ((c = getc(ifile)) != EOF) {
        putc(c, file);
    }
    fclose(file);
    exit(0);
}

static void makeutime(char *pp)
{
    int val;
    char *p;

    // p points to a user time
    p   = pp;
    val = 0;
    while (isdigit(*p)) {
        val = val * 10 + (*p++ - '0');
    }
    if (p - pp < 3)
        val *= HOUR;

    for (;;) {
        switch (*p) {
        case ':':
            ++p;
            if (isdigit(*p)) {
                if (isdigit(p[1])) {
                    val += (10 * *p + p[1] - 11 * '0');
                    p += 2;
                    continue;
                }
            }
            fprintf(stderr, "at: bad time format:\n");
            exit(1);

        case 'A':
        case 'a':
            if (val >= HALFDAY + HOUR)
                val = DAY + 1; // illegal
            if (val >= HALFDAY && val < (HALFDAY + HOUR))
                val -= HALFDAY;
            break;

        case 'P':
        case 'p':
            if (val >= HALFDAY + HOUR)
                val = DAY + 1; // illegal
            if (val < HALFDAY)
                val += HALFDAY;
            break;

        case 'n':
        case 'N':
            val = HALFDAY;
            break;

        case 'M':
        case 'm':
            val = 0;
            break;

        case '\0':
        case ' ':
            // 24 hour time
            if (val == DAY)
                val -= DAY;
            break;

        default:
            fprintf(stderr, "at: bad time format\n");
            exit(1);
        }
        break;
    }
    if (val < 0 || val >= DAY) {
        fprintf(stderr, "at: time out of range\n");
        exit(1);
    }
    if (val % HOUR >= 60) {
        fprintf(stderr, "at: illegal minute field\n");
        exit(1);
    }
    attime = val;
}

//
// argv[2], argv[3] are either month day OR weekday [week].  Returns 2 or 3, the last
// argument used.
//
static int makeuday(int argc, char **argv)
{
    time_t tm;
    int found = -1;
    int i;
    struct tm *detail;

    // first of all, what's today
    time(&tm);
    detail = localtime(&tm);
    uday = today = detail->tm_yday;
    uyear        = detail->tm_year + 1900;
    now          = detail->tm_hour * 100 + detail->tm_min;
    if (argc <= 2)
        return 1;
    // is the next argument a month name?
    for (i = 0; i < NMONTHS; i++) {
        if (prefix(argv[2], months[i])) {
            if (found < 0)
                found = i;
            else {
                fprintf(stderr, "at: ambiguous month\n");
                exit(1);
            }
        }
    }
    if (found >= 0) {
        if (argc <= 3)
            return 2;
        uday = atoi(argv[3]) - 1;
        if (uday < 0) {
            fprintf(stderr, "at: illegal day\n");
            exit(1);
        }
        while (--found >= 0)
            uday += mlen[found];
        if (dysize(detail->tm_year) == 366 && uday > 59)
            uday += 1;
        return 3;
    }
    // not a month, try day of week
    found = -1;
    for (i = 0; i < 7; i++) {
        if (prefix(argv[2], days[i])) {
            if (found < 0)
                found = i;
            else {
                fprintf(stderr, "at: ambiguous day of week\n");
                exit(1);
            }
        }
    }
    if (found < 0)
        return 1;
    // find next day of this sort
    uday = found - detail->tm_wday;
    if (uday <= 0)
        uday += 7;
    uday += today;
    if (argc > 3 && strcmp("week", argv[3]) == 0) {
        uday += 7;
        return 3;
    }
    return 2;
}

static char *prefix(char *begin, char *full)
{
    int c;

    while ((c = *begin++)) {
        if (isupper(c))
            c = tolower(c);
        if (*full != c)
            return NULL;
        else
            full++;
    }
    return full;
}

//
// yyyy.ddd.hhhh.uu -- sixteen characters against a DIRSIZ of 18.  The step of 53 is prime
// to 100, so a hundred tries cover every suffix and a hundred-and-first would repeat.
//
static void filename(char *dir, int y, int d, int t)
{
    int i, try;

    for (i = 0, try = 0; try < 100; i += 53, try++) {
        sprintf(fname, "%s/%04d.%03d.%04d.%02d", dir, y, d, t, (getpid() + i) % 100);
        if (access(fname, F_OK) == -1)
            return;
    }
    fprintf(stderr, "at: too many jobs at that time\n");
    exit(1);
}

static void onintr(int sig)
{
    (void)sig;
    unlink(fname);
    exit(1);
}
