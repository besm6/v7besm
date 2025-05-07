/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * Fake multiplexor routines to satisfy references
 * if you don't want it.
 */
// clang-format off
#include "sys/param.h"
#include "sys/dir.h"
#include "sys/user.h"
#include "sys/tty.h"
#include "sys/inode.h"
#include "sys/mx.h"
// clang-format on

sdata(cp) struct chan *cp;
{
}

mcttstart(tp) struct tty *tp;
{
}

mpxchan()
{
    u.u_error = EINVAL;
}

mcstart(p, q) struct chan *p;
caddr_t q;
{
}

scontrol(chan, s, c) struct chan *chan;
{
}
