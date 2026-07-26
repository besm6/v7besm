//
// clock() -- processor time used by the program (C11 §7.27.2.1).
//
// No v7 ancestor.  §7.27.2.1 asks for "the implementation's best approximation to the
// processor time used", divided by CLOCKS_PER_SEC to get seconds -- which is exactly
// what times() reports and what HZ counts, so CLOCKS_PER_SEC is HZ (250) in <time.h>
// and no scaling happens here.  User plus system time, because both are the program's.
//
// times() is the generated leaf (sys/syscalls.tbl); struct tms is four one-word fields,
// which is what both gates write (kernel/sys4.c, SYS_times in cmd/sim/syscall.cpp).
//
// Under b6sim the counters are the HOST's clock ticks, whose rate is not this
// machine's HZ.  That is the simulator's business and not a reason to scale here: on
// the kernel this port targets, a tick IS 1/HZ of a second.
//
#include <sys/param.h>
#include <sys/times.h>
#include <sys/types.h>
#include <time.h>

// <time.h> cannot include <sys/param.h> -- param.h is the kernel's, and time.h is on
// the user's side of the tree -- so CLOCKS_PER_SEC is a HAND-COPIED duplicate of HZ,
// and nothing checked the two still agreed.  This file is where a disagreement would
// land: it returns tick counts and lets the caller divide by CLOCKS_PER_SEC, so a stale
// copy makes every clock() in every program wrong by that ratio, silently.  param.h is
// #define-only (it says so at the top) and cmd/sh already includes it from userland, so
// pulling it in here costs nothing and closes the hole for good.
_Static_assert(CLOCKS_PER_SEC == HZ, "time.h's CLOCKS_PER_SEC must track sys/param.h's HZ");

int times(struct tms *buf);

clock_t clock(void)
{
    struct tms t;

    if (times(&t) == -1)
        return (clock_t)-1;
    return t.tms_utime + t.tms_stime;
}
