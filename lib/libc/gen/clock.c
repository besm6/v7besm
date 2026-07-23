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
#include <sys/times.h>
#include <sys/types.h>
#include <time.h>

int times(struct tms *buf);

clock_t clock(void)
{
    struct tms t;

    if (times(&t) == -1)
        return (clock_t)-1;
    return t.tms_utime + t.tms_stime;
}
