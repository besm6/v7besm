// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// The file-creation and file-control primitives.  v7 predates <fcntl.h>: each caller
// declared open()/creat() at its own head (the front end has no implicit
// declarations, so it had to).  This header is that declaration, written once, beside
// the O_* modes open() takes.
//
// The three modes are the v7 ones, and they are *small* on purpose.  open()'s second
// argument is 0/1/2; the kernel does `open1(ip, ++uap->rwmode, 0)' (kernel/sys2.c),
// so 0/1/2 become 1/2/3 == FREAD / FWRITE / FREAD|FWRITE (<sys/file.h>).
//
// What is deliberately NOT here, because this kernel is faithful v7:
//
//   - No O_CREAT / O_TRUNC / O_APPEND / O_EXCL / O_NDELAY.  open1() keeps only
//     `mode & (FREAD|FWRITE)' (kernel/sys2.c), so any higher bit is silently dropped.
//     Creation and truncation are reachable only through the separate creat() call,
//     which always truncates and hands back a write-only descriptor -- the dance
//     lib/libc/stdio/endopen.c does for "w"/"a" opens.
//   - No fcntl() and no F_* / FD_CLOEXEC: this kernel has no fcntl system call at all
//     (it is absent from kernel/sysent.c and <sys/syscall.h>).  The name of the header
//     is POSIX's; the contents are what v7 can honour.
//
// open() stays two-argument to match its arity in lib/libc/sys/syscalls.tbl and the
// gate in kernel/sysent.c; a variadic third argument would be meaningless with no
// O_CREAT.  The modes are `int' -- v7 has no mode_t in this contract.
#ifndef _FCNTL_H
#define _FCNTL_H

// open() access modes.  Not OR-able flags: they are the whole second argument.
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2

int open(const char *path, int mode);
int creat(const char *path, int mode);

#endif // _FCNTL_H
