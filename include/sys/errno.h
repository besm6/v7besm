// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// Error codes -- the numbering, and its one home.
//
// Both sides of the KERNEL gate read this file: the kernel assigns these to
// u.u_error and the $77 gate hands the number back in r14 (kernel/syscall.c),
// while <errno.h> adds the `errno' object C11 wants and leaves the numbers to
// this file.  It is #define-only for that reason, so that including it commits a
// translation unit to nothing at all.
//
// v7 wrote the list out twice, in <errno.h> and in <sys/user.h>'s `u_error codes'
// block, and this port inherited both.  They are gone: b6cpp rejects a macro
// redefinition unless the replacement text is character-identical, and clang-format's
// AlignConsecutiveMacros had already given EDOM and ERANGE different columns in the
// two files -- so a translation unit that named both headers did not compile.  One
// copy cannot drift from itself.
//
// One other copy of the numbering remains, and it is not a header: guest_errno() in
// cmd/sim/syscall.cpp, which maps a HOST errno onto these numbers, since b6sim
// services the syscalls on a machine with a numbering of its own.
//
// EMLINK is defined for completeness but is never returned by this kernel.  The tail
// of the list -- EDOM, ERANGE, EILSEQ -- is the C11 mandatory set (§7.5) and no
// syscall assigns any of the three: EDOM and ERANGE are math software and belong to
// libm, EILSEQ to the multibyte conversions of <wchar.h>/<uchar.h>.  All three are
// still numbered here, and still carry an entry in lib/libc/gen/errlst.c, so
// strerror() and perror() can name them.

#ifndef _SYS_ERRNO_H
#define _SYS_ERRNO_H

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

#endif // _SYS_ERRNO_H
