// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// Structure returned by times()
//
// <sys/types.h> is included rather than assumed, for the reason <sys/timeb.h>
// spells out: time_t comes from there, and this file sorts ahead of it.

#ifndef _SYS_TIMES_H
#define _SYS_TIMES_H

#include <sys/types.h>

struct tms {
    time_t tms_utime;  // user time
    time_t tms_stime;  // system time
    time_t tms_cutime; // user time, children
    time_t tms_cstime; // system time, children
};

#endif // _SYS_TIMES_H
