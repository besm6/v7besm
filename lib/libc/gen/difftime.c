/*
 * difftime(end, start) -- the seconds between two calendar times (C11 §7.27.2.2).
 *
 * No v7 ancestor: v7 had no such routine, because it had no <time.h> worth the name.
 * On a system where time_t is a plain one-word count of seconds the whole of it is the
 * subtraction; the double exists so that a portable caller may have an implementation
 * whose time_t is not arithmetic at all.
 *
 * A double is one 48-bit word with a 40-bit mantissa, which holds every difference a
 * 41-bit time_t can produce to the second.
 */
#include <time.h>

double difftime(time_t end, time_t start)
{
    return (double)(end - start);
}
