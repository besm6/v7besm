//
// time_t time(time_t *tloc) -- seconds since the epoch, also stored through tloc.
//
// The gate takes NO argument (sysent[13] is gtime, narg 0) and returns the time in the
// accumulator alone.  The store through the caller's pointer is v7 libc's own doing and
// is done here, which is why this is not a generated stub.
//
// `tloc' arrives in the accumulator, which the extracode overwrites, so it is parked in
// r9 first.  It is a `time_t *' -- a plain word address, not a fat pointer -- so
// fifteen bits hold it exactly.  Unlike wait.s, nothing needs saving across the store:
// `9 atx' writes the accumulator without destroying it, so the value is still there to
// be returned.
//
// gtime cannot fail, so there is no `14 v1m cerror'.  time_t is one 48-bit word here
// (include/sys/types.h), so there is none of the PDP-11 high/low splitting either.
//
#include <sys/syscall.h>

        .text
        .globl  time

time:
        ati     9               // r9 := tloc, before the trap destroys A
        $77     SYS_time
      9 vzm     tm_dn           // time(0): nowhere to store it
      9 atx                     // *tloc = the time; A survives the store
tm_dn:
     13 uj
