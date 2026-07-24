/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// The hook for locally added built-in commands.
//
// v7 shipped it as a stub returning 0 -- "not a built-in, go and fork" -- and so does
// this.  xec.c's command switch falls through to it for every name that is not in the
// `commands' table in msg.c.
//
#include "defs.h"

INT builtin(INT argn, STRING *com)
{
    (void)argn;
    (void)com;
    return 0;
}
