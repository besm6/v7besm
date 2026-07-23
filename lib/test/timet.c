/*
 * timet -- the whole of <time.h>, plus tell().
 *
 * Every calendar time below is a LITERAL, so the output is fixed: nothing here asks the
 * host what time it is.  The zone is fixed too, and by the system rather than by this
 * program -- b6sim's ftime() answers zone 0 and no daylight saving (cmd/sim/syscall.cpp)
 * -- so localtime() and gmtime() must agree, and %z must come out "+0000".  That is a
 * property of the simulator, not of the machine it runs on, so it is fair game for an
 * .expected file.
 *
 * The six instants are chosen for what they catch:
 *
 *      0               the epoch itself, a Thursday
 *      1000000000      an ordinary date well past 2^30 seconds
 *      951782400       29 February 2000 -- a century that IS a leap year
 *      4102444800      1 January 2100 -- a century that is NOT, which v7's `y % 4'
 *                      leap rule got wrong and a 41-bit time_t can reach
 *      -1              one second before the epoch: the floor division in gmtime()
 *      1583020800      1 March 2020, where %U and %W differ
 *
 * mktime() is checked by round trip: every one of them must come back from the struct
 * tm that localtime() made of it.  It is also given a deliberately out-of-range
 * struct -- month 13, hour 25 -- because §7.27.2.3 says it must fold those in.
 *
 * strftime is walked over the whole C11 conversion set, one line each, so a specifier
 * that came out empty or shifted is visible rather than merely wrong somewhere.
 *
 * clock() prints nothing: the number is however much processor time this run used, and
 * that is the one thing here that would differ between two runs.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

char *timezone(int zone, int dst);
int dysize(int y);
int tell(int f);
int creat(const char *path, int mode);
int close(int fd);
int unlink(const char *path);
int write(int fd, const char *buf, int n);
int lseek(int fd, int off, int whence);

static const time_t when[] = { 0, 1000000000, 951782400, 4102444800, -1, 1583020800 };

static void ok(const char *what, int cond)
{
    printf("%s %s\n", cond ? "ok  " : "FAIL", what);
}

/* Every field of a struct tm, so a conversion that lands one off is not hidden. */
static void showtm(const char *what, const struct tm *t)
{
    printf("%s %d-%02d-%02d %02d:%02d:%02d wday %d yday %d isdst %d\n", what, t->tm_year + 1900,
           t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec, t->tm_wday, t->tm_yday,
           t->tm_isdst);
}

static void one(const char *fmt, const struct tm *t)
{
    char buf[64];
    size_t n;

    n = strftime(buf, sizeof buf, fmt, t);
    printf("%-4s [%s] %d\n", fmt, buf, (int)n);
}

int main(void)
{
    struct tm tmv, *t;
    time_t c, back;
    char buf[64];
    unsigned i;
    int fd;

    /* ---- gmtime, localtime, asctime, ctime ---- */
    for (i = 0; i < sizeof when / sizeof when[0]; i++) {
        c = when[i];
        t = gmtime(&c);
        printf("--- %d\n", (int)c);
        showtm("gm ", t);
        printf("asctime %s", asctime(t));
        tmv = *t;

        t = localtime(&c);
        showtm("loc", t);
        ok("localtime agrees with gmtime at zone 0",
           t->tm_year == tmv.tm_year && t->tm_yday == tmv.tm_yday && t->tm_hour == tmv.tm_hour &&
               t->tm_sec == tmv.tm_sec);
        printf("ctime   %s", ctime(&c));

        /* The round trip: the struct localtime just made must convert back. */
        tmv  = *t;
        back = mktime(&tmv);
        printf("mktime  %d\n", (int)back);
        ok("mktime round-trips", back == c);
    }

    /* ---- leap years, which is where v7's rule ran out ---- */
    printf("--- dysize\n");
    ok("1970 has 365 days", dysize(70) == 365);
    ok("1972 has 366 days", dysize(72) == 366);
    ok("2000 has 366 days", dysize(100) == 366);
    ok("2100 has 365 days", dysize(200) == 365);

    /* ---- mktime normalizes what the caller writes ---- */
    printf("--- mktime normalizing\n");
    tmv.tm_year  = 100; /* 2000 */
    tmv.tm_mon   = 13;  /* -> February 2001 */
    tmv.tm_mday  = 32;  /* -> 4 March */
    tmv.tm_hour  = 25;  /* -> 01:00 the next day */
    tmv.tm_min   = 0;
    tmv.tm_sec   = 0;
    tmv.tm_isdst = 0;
    c            = mktime(&tmv);
    printf("normalized %d\n", (int)c);
    showtm("nrm", &tmv);

    /* ---- difftime ---- */
    printf("--- difftime\n");
    printf("difftime %d\n", (int)difftime(1000000000, 999999999));
    ok("difftime is signed", difftime(0, 60) == -60.0);
    ok("difftime of equal times is zero", difftime(1583020800, 1583020800) == 0.0);

    /* ---- strftime, one line per conversion ---- */
    printf("--- strftime 2020-03-01 00:00:00\n");
    c = when[5];
    t = gmtime(&c);
    one("%a", t);
    one("%A", t);
    one("%b", t);
    one("%B", t);
    one("%c", t);
    one("%C", t);
    one("%d", t);
    one("%D", t);
    one("%e", t);
    one("%F", t);
    one("%g", t);
    one("%G", t);
    one("%h", t);
    one("%H", t);
    one("%I", t);
    one("%j", t);
    one("%m", t);
    one("%M", t);
    one("%n", t);
    one("%p", t);
    one("%r", t);
    one("%R", t);
    one("%S", t);
    one("%t", t);
    one("%T", t);
    one("%u", t);
    one("%U", t);
    one("%V", t);
    one("%w", t);
    one("%W", t);
    one("%x", t);
    one("%X", t);
    one("%y", t);
    one("%Y", t);
    one("%z", t);
    one("%Z", t);
    one("%%", t);
    one("%Ey", t); /* the E and O modifiers are ignored in the C locale */
    one("%Od", t);

    printf("--- strftime at the year boundary\n");
    c = when[4]; /* 31 December 1969: ISO week 1 of 1970 */
    t = gmtime(&c);
    one("%G", t);
    one("%V", t);
    one("%u", t);
    c = when[3]; /* 1 January 2100: ISO week 53 of 2099 */
    t = gmtime(&c);
    one("%G", t);
    one("%V", t);

    printf("--- strftime bounds\n");
    c = when[0];
    t = gmtime(&c);
    ok("a result that just fits is written", strftime(buf, 5, "%Y", t) == 4);
    ok("and is terminated", strcmp(buf, "1970") == 0);
    ok("a result one short returns 0", strftime(buf, 4, "%Y", t) == 0);
    ok("a zero size returns 0", strftime(buf, 0, "%Y", t) == 0);

    /* ---- timezone ---- */
    printf("--- timezone\n");
    printf("timezone(0,0)    %s\n", timezone(0, 0));
    printf("timezone(300,0)  %s\n", timezone(300, 0));
    printf("timezone(300,1)  %s\n", timezone(300, 1));
    printf("timezone(480,0)  %s\n", timezone(480, 0));
    printf("timezone(90,0)   %s\n", timezone(90, 0));
    printf("timezone(-90,0)  %s\n", timezone(-90, 0));

    /* ---- clock: it must answer, but the answer is not repeatable ---- */
    ok("clock answers", clock() != (clock_t)-1);

    /* ---- tell ---- */
    printf("--- tell\n");
    fd = creat("timet.tmp", 0666);
    ok("the scratch file opened", fd >= 0);
    ok("tell starts at zero", tell(fd) == 0);
    ok("nine bytes written", write(fd, "123456789", 9) == 9);
    ok("tell follows the write", tell(fd) == 9);
    lseek(fd, 3, 0);
    ok("tell follows a seek", tell(fd) == 3);
    close(fd);
    unlink("timet.tmp");

    printf("done\n");
    return 0;
}
