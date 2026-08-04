// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// <sys/types.h> is included rather than assumed, for the reason <sys/timeb.h>
// spells out: dev_t, ino_t, off_t and time_t all come from there, and this file
// sorts ahead of it in an include list clang-format has put in order.
//
// The five calls at the foot are the ones <unistd.h> deliberately left out --
// they belong beside `struct stat' and the mode bits, and this is that file.
// The symbols and arities are the contract in lib/libc/sys/syscalls.tbl and the
// gate in kernel/sysent.c; the numbers are in <sys/syscall.h>.  The modes are
// plain `int': this kernel has no mode_t, and <fcntl.h> says the same of open().

#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#include <sys/types.h>

struct stat {
    dev_t st_dev;
    ino_t st_ino;
    int st_mode;
    int st_nlink;
    int st_uid;
    int st_gid;
    dev_t st_rdev;
    off_t st_size;
    time_t st_atime;
    time_t st_mtime;
    time_t st_ctime;
};

#define S_IFMT   0170000 // type of file
#define S_IFDIR  0040000 // directory
#define S_IFCHR  0020000 // character special
#define S_IFBLK  0060000 // block special
#define S_IFREG  0100000 // regular
#define S_IFMPC  0030000 // multiplexed char special
#define S_IFMPB  0070000 // multiplexed block special
#define S_ISUID  0004000 // set user id on execution
#define S_ISGID  0002000 // set group id on execution
#define S_ISVTX  0001000 // save swapped text even after use
#define S_IREAD  0000400 // read permission, owner
#define S_IWRITE 0000200 // write permission, owner
#define S_IEXEC  0000100 // execute/search permission, owner

// The nine permission bits under their POSIX names.  v7 named only the owner's
// three (above) and left group and other to be written as shifts of them, which
// is how its own commands spell them; POSIX names all nine, and a program that
// formats an `ls -l' permission string wants all nine by name.  The bits are the
// same bits -- S_IRUSR IS S_IREAD -- so this adds spellings, not meanings.
// cmd/ar/list.c is the caller that wanted them.
#define S_IRUSR  0000400 // read permission, owner
#define S_IWUSR  0000200 // write permission, owner
#define S_IXUSR  0000100 // execute/search permission, owner
#define S_IRGRP  0000040 // read permission, group
#define S_IWGRP  0000020 // write permission, group
#define S_IXGRP  0000010 // execute/search permission, group
#define S_IROTH  0000004 // read permission, other
#define S_IWOTH  0000002 // write permission, other
#define S_IXOTH  0000001 // execute/search permission, other

// Not for the kernel side: it includes this header for `struct stat', and all five of
// these names are ALSO its own system-call handlers -- `void stat(void)' and friends,
// declared in <sys/systm.h> and defined in kernel/sys3.c.  Same spelling, opposite side
// of the gate.  sys/map.h and sys/tty.h split their two audiences the same way.
//
// KERNEL is the whole of the test, and it means "this translation unit is kernel-side"
// rather than "this object goes in the kernel image": the standalone programs in
// kernel/test/ link kernel objects and include <sys/systm.h>, so they are compiled with
// -DKERNEL too.  This guard used to carry a second condition, `!defined(_SYS_SYSTM_H)',
// because they were not -- an include ORDER dependency wearing systm.h's own guard macro
// as a disguise, and one that held only for as long as every kernel source happened to
// include <sys/systm.h> ahead of this file.  `sys/stat.h' sorts BEFORE `sys/systm.h'.
#ifndef KERNEL
int stat(const char *path, struct stat *buf);
int fstat(int fd, struct stat *buf);
int chmod(const char *path, int mode);
int mknod(const char *path, int mode, dev_t dev);
int umask(int mask);
#endif

#endif // _SYS_STAT_H
