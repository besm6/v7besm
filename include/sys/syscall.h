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
// (setpgrp), 40 (tell), 49, 50, 55-58, 62, 63 -- get none, so that naming one
// cannot be mistaken for implementing it.
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

#define SYS_exit   1
#define SYS_fork   2
#define SYS_read   3
#define SYS_write  4
#define SYS_open   5
#define SYS_close  6
#define SYS_wait   7
#define SYS_creat  8
#define SYS_link   9
#define SYS_unlink 10
#define SYS_exec   11
#define SYS_chdir  12
#define SYS_time   13
#define SYS_mknod  14
#define SYS_chmod  15
#define SYS_chown  16
#define SYS_break  17
#define SYS_stat   18
#define SYS_seek   19
#define SYS_getpid 20
#define SYS_mount  21
#define SYS_umount 22
#define SYS_setuid 23
#define SYS_getuid 24
#define SYS_stime  25
#define SYS_ptrace 26
#define SYS_alarm  27
#define SYS_fstat  28
#define SYS_pause  29
#define SYS_utime  30
#define SYS_stty   31
#define SYS_gtty   32
#define SYS_access 33
#define SYS_nice   34
#define SYS_ftime  35
#define SYS_sync   36
#define SYS_kill   37
#define SYS_dup    41
#define SYS_pipe   42
#define SYS_times  43
#define SYS_profil 44
#define SYS_sigret 45
#define SYS_setgid 46
#define SYS_getgid 47
#define SYS_signal 48
#define SYS_acct   51
#define SYS_phys   52
#define SYS_lock   53
#define SYS_ioctl  54
#define SYS_exece  59
#define SYS_umask  60
#define SYS_chroot 61

#endif // _SYS_SYSCALL_H
