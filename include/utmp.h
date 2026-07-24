// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// ut_time is a time_t, where v7 wrote `long'.  It costs nothing and buys the
// type check: on this machine `long' and `int' are the same 41-bit word (see
// <sys/types.h>), so the record's layout is byte-for-byte what v7 wrote -- but
// time_t is `int', so `time(&u.ut_time)' does not compile with the `long'.
#ifndef _UTMP_H
#define _UTMP_H

#include <sys/types.h> // time_t

struct utmp {
    char ut_line[8]; // tty name
    char ut_name[8]; // user id
    time_t ut_time;  // time on
};

#endif // _UTMP_H
