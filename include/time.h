// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// <time.h> -- date and time (C11 §7.27).
//
// Nothing here is implemented yet: ctime, localtime, asctime and the timezone
// machinery are lib phase 5, and clock() waits on a per-process CPU-time counter
// worth the name.  v7's header held struct tm and nothing else, because v7 left
// the declarations to the manual and let K&R's implicit int cover the calls;
// there is no implicit anything now.
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

// ---- declared for future implementation: lib phase 5 (TODO) ----
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
