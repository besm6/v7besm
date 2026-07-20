/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 *	Memory special file
 *	minor device 0 is physical memory
 *	minor device 1 is kernel memory
 *	minor device 2 is EOF/RATHOLE
 *
 * Physical memory above 0100000 is out of the unmapped kernel's reach and needs a
 * copyseg-style mapped bracket to get at, so minors 0 and 1 are stubbed out for
 * now.  Minor 2 (/dev/null) works.
 * See kernel/TODO.md, "Deferred, owned by no task".
 */

// clang-format off
#include "sys/types.h"
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/dir.h"
#include "sys/user.h"
#include "sys/conf.h"
// clang-format on

void mmread(int dev)
{
    if (minor(dev) == 2)
        return;
    u.u_error = ENXIO;
}

void mmwrite(int dev)
{
    if (minor(dev) == 2) {
        u.u_count = 0;
        return;
    }
    u.u_error = ENXIO;
}
