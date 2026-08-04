// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// System call numbers -- the N of the `$77 N' extracode that opens the kernel's
// gate (kernel/syscall.c, doc/Aout_Simulator.md).
//
// kernel/sysent.c is the authority, and it is POSITIONAL: the number of a call is
// its row index in that array, so this header can only put a name to each row --
// it cannot drive them.  Change one and change the other.
//
// This header is #define-only so that the assembly leaves in lib/libc/sys/ can
// include it, which is what makes them `.S' rather than `.s' -- b6cc dispatches a
// .S through the preprocessor and a .s straight to the assembler (lib/rules.mk).
//
// THE GAPS ARE DELIBERATE.  Only the calls this kernel implements get a name; the
// rows that are nullsys or nosys in sysent.c -- 0 (indir), 38 (switch), 39
// (setpgrp), 40 (tell), 50, 55-58, 62, 63 -- get none, so that naming one
// cannot be mistaken for implementing it.
//
// ONE NUMBER IS NOT v7's.  Row 49 was "reserved for USG" and is kctl, this port's own
// call: there is no /unix on the root filesystem, so the nlist(3) route to a kernel
// variable does not exist here and something had to take its place (<sys/kctl.h>).  It
// took the lowest free row rather than a number past 63, which would have moved NSYSENT.
//
// ONE NAME IS NOT A CALL ANY PROGRAM MAKES.  Row 45, v7's "unused", is sigreturn:
// the kernel plants a `$77 SYS_sigret' word on the user stack itself (`sigcode',
// kernel/besm6.S) as the return address of a signal handler, so the number is issued
// by an instruction the kernel assembled and there is no libc leaf for it -- it is
// absent from lib/libc/sys/syscalls.tbl on purpose.  See kernel/sendsig.c.
//
// THREE NAMES ARE THE KERNEL'S, NOT LIBC'S, and are spelled here as sysent.c
// spells them: SYS_seek is what lseek() issues, SYS_break is what sbrk() issues
// through the _break() leaf, and SYS_exece is v7's spelling of execve.  Two go the
// other way, where sysent.c's row comment is an abbreviation rather than a name:
// SYS_profil is its "44 = prof" and SYS_signal its "48 = sig".  There are
// deliberately no aliases -- one number, one name, or this header reintroduces the
// disagreement it exists to remove.  lib/libc/sys/syscalls.tbl is where a libc
// symbol is mapped to the macro it issues.
//
// cmd/sim/syscall.cpp keeps a copy of this list as its own enum, and must be kept
// in step by hand: b6sim is a HOST tool and cannot have include/ on its -I path,
// which would shadow the <stdio.h>, <errno.h>, <sys/stat.h> and <sys/times.h> it
// includes for real.
#ifndef _SYS_SYSCALL_H
#define _SYS_SYSCALL_H

#define SYS_exit   1  // void _exit(int status) -- does not return
#define SYS_fork   2  // pid_t fork(void) -- other side's pid in A, 1 in r12 for the child
#define SYS_read   3  // int read(int fd, char *buf, int n)
#define SYS_write  4  // int write(int fd, char *buf, int n)
#define SYS_open   5  // int open(char *path, int mode)
#define SYS_close  6  // int close(int fd)
#define SYS_wait   7  // pid_t wait(void) -- no argument: the status comes back in r12
#define SYS_creat  8  // int creat(char *path, int mode)
#define SYS_link   9  // int link(char *target, char *linkname)
#define SYS_unlink 10 // int unlink(char *path)
#define SYS_exec   11 // int exec(char *path, char **argv) -- returns only on failure
#define SYS_chdir  12 // int chdir(char *path)
#define SYS_time   13 // time_t time(void) -- libc's tloc store is libc's own doing
#define SYS_mknod  14 // int mknod(char *path, int mode, int dev)
#define SYS_chmod  15 // int chmod(char *path, int mode)
#define SYS_chown  16 // int chown(char *path, int uid, int gid)
#define SYS_break  17 // int _break(char *addr) -- what sbrk() issues
#define SYS_stat   18 // int stat(char *path, struct stat *buf)
#define SYS_seek   19 // off_t lseek(int fd, off_t off, int whence) -- off_t is one word
#define SYS_getpid 20 // pid_t getpid(void) -- the parent's pid in r12
#define SYS_mount  21 // int mount(char *spec, char *dir, int rdonly)
#define SYS_umount 22 // int umount(char *spec)
#define SYS_setuid 23 // int setuid(int uid)
#define SYS_getuid 24 // int getuid(void) -- the real uid in A, the effective one in r12
#define SYS_stime  25 // int stime(time_t t) -- by VALUE; time_t is one word
#define SYS_ptrace 26 // int ptrace(int req, int pid, int *addr, int data)
#define SYS_alarm  27 // int alarm(int sec)
#define SYS_fstat  28 // int fstat(int fd, struct stat *buf)
#define SYS_pause  29 // int pause(void)
#define SYS_utime  30 // int utime(char *path, time_t *times)
#define SYS_stty   31 // int stty(int fd, struct sgttyb *buf)
#define SYS_gtty   32 // int gtty(int fd, struct sgttyb *buf)
#define SYS_access 33 // int access(char *path, int mode)
#define SYS_nice   34 // int nice(int incr)
#define SYS_ftime  35 // int ftime(struct timeb *tp)
#define SYS_sync   36 // int sync(void)
#define SYS_kill   37 // int kill(pid_t pid, int sig)
#define SYS_dup    41 // int dup(int fd, int fd2) -- bit 0100 of fd asks for dup2
#define SYS_pipe   42 // int pipe(void) -- no argument: read end in A, write end in r12
#define SYS_times  43 // int times(struct tms *buf)
#define SYS_profil 44 // int profil(char *buf, int n, int off, int scale)
#define SYS_sigret 45 // void sigreturn(void) -- issued by the kernel's sigcode, not libc
#define SYS_setgid 46 // int setgid(int gid)
#define SYS_getgid 47 // int getgid(void) -- the real gid in A, the effective one in r12
#define SYS_signal 48 // int (*signal(int sig, int (*func)()))()
#define SYS_kctl   49 // int kctl(const char *name, int op, void *buf, int len)
#define SYS_acct   51 // int acct(char *path)
#define SYS_phys   52 // int phys(int segno, int npages, int physaddr)
#define SYS_lock   53 // int lock(int flag)
#define SYS_ioctl  54 // int ioctl(int fd, int req, char *argp)
#define SYS_exece  59 // int exece(char *path, char **argv, char **envp) -- see SYS_exec
#define SYS_umask  60 // int umask(int mask)
#define SYS_chroot 61 // int chroot(char *path)

#endif // _SYS_SYSCALL_H
