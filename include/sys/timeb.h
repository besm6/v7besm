// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// Structure returned by ftime system call

#ifndef _SYS_TIMEB_H
#define _SYS_TIMEB_H

struct timeb {
    time_t time;
    int millitm;
    int timezone;
    int dstflag;
};

#endif // _SYS_TIMEB_H
