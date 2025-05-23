/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * Structure returned by times()
 */
struct tms {
    time_t tms_utime;  /* user time */
    time_t tms_stime;  /* system time */
    time_t tms_cutime; /* user time, children */
    time_t tms_cstime; /* system time, children */
};
