// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// The callout structure is for
// a routine arranging
// to be called by the clock interrupt
// (clock.c) with a specified argument,
// in a specified amount of time.
// Used, for example, to time tab
// delays on typewriters.

#ifndef _SYS_CALLO_H
#define _SYS_CALLO_H

struct callo {
    int c_time;             // incremental time
    carg_t c_arg;           // argument to routine
    void (*c_func)(carg_t); // routine
};
struct callo callout[NCALL];

#endif // _SYS_CALLO_H
