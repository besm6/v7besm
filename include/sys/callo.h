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

#include <sys/param.h> // NCALL -- included, not assumed; see sys/dir.h
#include <sys/types.h> // carg_t

struct callo {
    int c_time;             // incremental time
    carg_t c_arg;           // argument to routine
    void (*c_func)(carg_t); // routine
};

// Declared, not defined: v7 wrote this without the `extern' and got away with it on a
// header nothing much included.  A tentative definition in a header gives every includer
// an array of its own, which is a link error waiting for a second one.  kernel/clock.c
// defines it -- the only file that touches it -- and is linked into both images that
// need it, the kernel and kernel/test's `uclock'.
extern struct callo callout[NCALL];

#endif // _SYS_CALLO_H
