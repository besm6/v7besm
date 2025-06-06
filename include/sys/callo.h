/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * The callout structure is for
 * a routine arranging
 * to be called by the clock interrupt
 * (clock.c) with a specified argument,
 * in a specified amount of time.
 * Used, for example, to time tab
 * delays on typewriters.
 */

struct callo {
    int c_time;             /* incremental time */
    caddr_t c_arg;          /* argument to routine */
    void (*c_func)(caddr_t); /* routine */
};
struct callo callout[NCALL];
