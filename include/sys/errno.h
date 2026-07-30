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

#define EPERM   1   // Not owner
#define ENOENT  2   // No such file or directory
#define ESRCH   3   // No such process
#define EINTR   4   // Interrupted system call
#define EIO     5   // I/O error
#define ENXIO   6   // No such device or address
#define E2BIG   7   // Arg list too long
#define ENOEXEC 8   // Exec format error
#define EBADF   9   // Bad file number
#define ECHILD  10  // No children
#define EAGAIN  11  // No more processes
#define ENOMEM  12  // Not enough core
#define EACCES  13  // Permission denied
#define EFAULT  14  // Bad address
#define ENOTBLK 15  // Block device required
#define EBUSY   16  // Mount device busy
#define EEXIST  17  // File exists
#define EXDEV   18  // Cross-device link
#define ENODEV  19  // No such device
#define ENOTDIR 20  // Not a directory
#define EISDIR  21  // Is a directory
#define EINVAL  22  // Invalid argument
#define ENFILE  23  // File table overflow
#define EMFILE  24  // Too many open files
#define ENOTTY  25  // Not a typewriter
#define ETXTBSY 26  // Text file busy
#define EFBIG   27  // File too large
#define ENOSPC  28  // No space left on device
#define ESPIPE  29  // Illegal seek
#define EROFS   30  // Read-only file system
#define EMLINK  31  // Too many links
#define EPIPE   32  // Broken pipe

// The C11 mandatory three -- library codes, never assigned by the kernel.
#define EDOM   33   // Argument too large
#define ERANGE 34   // Result too large
#define EILSEQ 35   // Illegal byte sequence

#endif // _SYS_ERRNO_H
