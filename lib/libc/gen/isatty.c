/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * Returns 1 iff the descriptor is a terminal.
 *
 * gtty() is the whole test: it is the one syscall that fails with ENOTTY on anything
 * that is not a tty, and the parameters it fills in are thrown away.  struct sgttyb
 * is five words here -- four chars, each in a word of its own, and an int -- which is
 * what both gates write (kernel/dev/tty.c, cmd/sim/syscall.cpp).
 */
#include <sgtty.h>

int gtty(int fd, struct sgttyb *buf);

int isatty(int f)
{
    struct sgttyb ttyb;

    if (gtty(f, &ttyb) < 0)
        return 0;
    return 1;
}
