// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// Error codes.
//
// This list is the user-level copy, and it is duplicated in two other places
// that must stay in step with it, number for number:
//
//      include/sys/user.h      the `u_error codes' block -- what the kernel
//                              itself assigns to u.u_error, and what the $77
//                              gate hands back in r14 (kernel/syscall.c).
//      cmd/sim/syscall.cpp     guest_errno(), which maps a HOST errno onto
//                              these numbers, since b6sim services the syscalls
//                              on a machine with a numbering of its own.
//
// EMLINK is defined for completeness but is never returned by this kernel.  The
// tail of the list -- EDOM, ERANGE, EILSEQ -- is the C11 mandatory set (§7.5),
// and none of the three is a kernel code: no syscall assigns them, so they
// appear in neither of the two copies above.  EDOM and ERANGE are math
// software and belong to libm; EILSEQ belongs to the multibyte conversions of
// <wchar.h>/<uchar.h>.  All three are still numbered here, and still carry an
// entry in lib/libc/gen/errlst.c, so strerror() and perror() can name them.
#ifndef _ERRNO_H
#define _ERRNO_H

// C11 §7.5 requires errno to be a MACRO expanding to a modifiable int lvalue,
// not merely an object of that name.  The object is the one word in
// lib/libc/sys/cerror.s, which is the only thing that ever writes it; the
// self-referential macro below meets the letter of the standard at no cost,
// since the no-recursive-expansion rule resolves the inner name back to it.
extern int errno;
#define errno errno

#define EPERM   1
#define ENOENT  2
#define ESRCH   3
#define EINTR   4
#define EIO     5
#define ENXIO   6
#define E2BIG   7
#define ENOEXEC 8
#define EBADF   9
#define ECHILD  10
#define EAGAIN  11
#define ENOMEM  12
#define EACCES  13
#define EFAULT  14
#define ENOTBLK 15
#define EBUSY   16
#define EEXIST  17
#define EXDEV   18
#define ENODEV  19
#define ENOTDIR 20
#define EISDIR  21
#define EINVAL  22
#define ENFILE  23
#define EMFILE  24
#define ENOTTY  25
#define ETXTBSY 26
#define EFBIG   27
#define ENOSPC  28
#define ESPIPE  29
#define EROFS   30
#define EMLINK  31
#define EPIPE   32

// The C11 mandatory three -- library codes, never assigned by the kernel.
#define EDOM   33
#define ERANGE 34
#define EILSEQ 35

#endif // _ERRNO_H
