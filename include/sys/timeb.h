// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// Structure returned by ftime system call
//
// <sys/types.h> is included rather than assumed: time_t below comes from there,
// and v7 left it to the caller to have included it first.  That is a trap once
// clang-format is sorting include lists -- "sys/timeb.h" sorts BEFORE
// "sys/types.h" -- so the header now stands on its own.

#ifndef _SYS_TIMEB_H
#define _SYS_TIMEB_H

#include <sys/types.h>

struct timeb {
    time_t time;
    int millitm;
    int timezone;
    int dstflag;
};

#endif // _SYS_TIMEB_H
