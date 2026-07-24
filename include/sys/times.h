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

// The call this structure exists for.  v7 declared it in no header, so every caller
// wrote its own extern; the shell's `times' built-in is the first one in this tree that
// needs it (cmd/sh/xec.c).
//
// Guarded like <sys/stat.h>'s block, and for the same reason spelled out there: the
// kernel has a syscall handler of this name with a different shape (`void times(void)',
// declared in <sys/systm.h>), and the two must never both be in scope.  The second
// condition catches the kernel-side sources that are compiled WITHOUT -DKERNEL.
#if !defined(KERNEL) && !defined(_SYS_SYSTM_H)
int times(struct tms *buf);
#endif

#endif // _SYS_TIMES_H
