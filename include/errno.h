// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// The C11 §7.5 header: `errno' itself, over the numbering in <sys/errno.h>.
//
// v7 put the codes here and copied them into <sys/user.h> for the kernel; both copies
// are now one file, which this one includes.  The split is the arrangement every Unix
// after v7 uses, and it is what keeps the `errno' object out of the kernel: the kernel
// includes <sys/errno.h> (through <sys/user.h>) and never declares an errno.

#ifndef _ERRNO_H
#define _ERRNO_H

#include <sys/errno.h>

// C11 §7.5 requires errno to be a MACRO expanding to a modifiable int lvalue,
// not merely an object of that name.  The object is the one word in
// lib/libc/sys/cerror.s, which is the only thing that ever writes it; the
// self-referential macro below meets the letter of the standard at no cost,
// since the no-recursive-expansion rule resolves the inner name back to it.
extern int errno;
#define errno errno

#endif // _ERRNO_H
