// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// <time.h> -- date and time (C11 §7.27).
//
// All of it is in libc.a now (lib phase 5).  v7's header held struct tm and
// nothing else, because v7 left the declarations to the manual and let K&R's
// implicit int cover the calls; there is no implicit anything now.  Four of the
// routines below had no v7 ancestor at all -- clock, difftime, mktime and
// strftime -- and are written from §7.27; the other five are v7's, in
// lib/libc/gen/ctime.c, with the two places where its arithmetic ran out noted
// there (a 41-bit time_t reaches past 2100, which a `y % 4' leap rule does not).
//
// THE ZONE COMES FROM ftime(), as it did in v7: the kernel owns the westward
// offset and the daylight flag, and there is no zoneinfo to read.  That is what
// localtime, mktime and strftime's %z/%Z all ask.  v7's timezone(), which turns
// such an offset into a name, is lib/libc/gen/timezone.c and is declared by no
// header -- C11 has %Z instead, and a caller that wants the v7 spelling declares
// it itself.  So does dysize(), for the same reason.
//
// time_t is 41 bits of seconds -- no 2038 problem -- and is a plain `int' like
// every other scalar here (sys/types.h explains why signedness is the only thing
// the spelling still chooses).  It has to be defined in BOTH files, since the
// kernel includes sys/types.h and never this one; the shared _TIME_T marker is
// what keeps a source that includes both from seeing the typedef twice.
//
// CLOCKS_PER_SEC is HZ, the 250-per-second tick this kernel programs
// (sys/param.h): clock() can be no finer than the clock the machine has.
#ifndef _TIME_H
#define _TIME_H

#include <stddef.h>

#ifndef _TIME_T
#define _TIME_T
typedef int time_t;
#endif

typedef int clock_t;

#define CLOCKS_PER_SEC 250

struct tm { // see ctime(3)
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

// ---- implemented in libc.a ----
clock_t clock(void);
time_t time(time_t *tloc);
double difftime(time_t end, time_t start);
time_t mktime(struct tm *tp);

struct tm *localtime(const time_t *clk);
struct tm *gmtime(const time_t *clk);
char *asctime(const struct tm *tp);
char *ctime(const time_t *clk);
size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tp);

#endif // _TIME_H
