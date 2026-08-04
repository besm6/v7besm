// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// The low-level I/O and process primitives -- the libc leaves that go straight to
// a system call, and the handful built directly on them.  v7 predates <unistd.h>:
// each of its callers used to declare the syscall it needed at its own head, the
// same block copied across a dozen files (the front end has no implicit
// declarations, so it had to).  This header is that block, written once.  The
// symbols and arities are the contract in lib/libc/sys/syscalls.tbl and the
// hand-written wrappers beside it; the numbers are in <sys/syscall.h>.
//
// POSIX names, with two BESM-6 concessions the data model forces:
//
//   - Counts and durations are signed `int', not POSIX's `unsigned': unsigned
//     arithmetic is a library call on this machine (<sys/types.h>), and a signed
//     word holds every count a syscall can be handed.  size_t itself is signed
//     here (<stddef.h>), so `read'/`write' read naturally as the POSIX prototypes.
//   - `sbrk'/`brk' keep `char *', matching lib/libc/sys/sbrk.c, which counts the
//     break in whole char units; `void *' would not match the definition.
//
// What is NOT here, and where it went instead: `open'/`creat' are <fcntl.h>;
// `mknod', `chmod', `umask' and `stat'/`fstat' are <sys/stat.h>, beside the mode
// bits and the struct they are about; `wait' is <sys/wait.h>, beside the macros
// that take its status apart; and the two calls v7 had no counterpart for are with
// the structures they fill -- `kctl' in <sys/kctl.h>, beside the operation codes,
// and `statfs' in <sys/statfs.h>.  This tree now has all five of those headers.
#ifndef _UNISTD_H
#define _UNISTD_H

#include <sys/types.h> // off_t, ssize_t, pid_t, uid_t, gid_t
#include <stddef.h>    // size_t, NULL

// The three descriptors every process starts with.
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

// access() modes.  F_OK asks only about existence; the rest are OR-able.
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

// lseek() whence.  Also in <stdio.h>; identical, so a source may have both.
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

// I/O.
ssize_t read(int fd, void *buf, size_t n);
ssize_t write(int fd, const void *buf, size_t n);
int close(int fd);
off_t lseek(int fd, off_t off, int whence);
int dup(int fd);
int dup2(int oldfd, int newfd);
int pipe(int fd[2]);

// The file system.
int access(const char *path, int mode);
int unlink(const char *path);
int link(const char *target, const char *linkname);
int chdir(const char *path);
int chroot(const char *path);
int chown(const char *path, uid_t uid, gid_t gid);
int sync(void);

// mount(2)/umount(2).  `spec' is a BLOCK special file -- getmdev() (kernel/sys3.c) refuses
// anything else with ENOTBLK, so it is /dev/md1 and not the raw /dev/rmd1 -- and `rdonly'
// non-zero mounts without write permission (s_ronly, kernel/filsys.h).  v7 declared neither
// anywhere: /etc/mount and /etc/umount were the only callers and relied on the implicit int.
// There is no <sys/mount.h> for a program to include; that header is the kernel's own mount
// table and says nothing about these two.
int mount(const char *spec, const char *dir, int rdonly);
int umount(const char *spec);

// utime() takes a TWO-ELEMENT time_t VECTOR -- `accessed' then `updated' -- and not POSIX's
// `struct utimbuf'.  That is the kernel's own interface: sys4.c copyin()s `time_t tv[2]', and
// lib/libc/man/utime.2 has said so since it was corrected.  There is no <utime.h> in this
// tree, v7 having had none, so the declaration lives here with the other file-system calls.
int utime(const char *file, const time_t timep[2]);

// Processes.  exece() is v7's spelling of execve, and the symbol libc backs.
//
// execlp() and execvp() are libc's own $PATH-searching forms (lib/libc/gen/), and they are
// declared HERE for the first time: v7 put them in no header at all, so every caller in this
// tree used to open with a declaration of its own.  exec.2 has named them all along.
pid_t fork(void);
_Noreturn void _exit(int status);
int execl(const char *path, const char *arg0, ...);
int execle(const char *path, const char *arg0, ...);
int execlp(const char *file, const char *arg0, ...);
int execv(const char *path, char *const argv[]);
int execvp(const char *file, char *const argv[]);
int exece(const char *path, char *const argv[], char *const envp[]);
extern char **environ; // crt0's; see lib/libc/gen/getenv.c

// Real and effective ids.
pid_t getpid(void);
pid_t getppid(void);
uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
gid_t getegid(void);
int setuid(uid_t uid);
int setgid(gid_t gid);

// The rest.
int nice(int incr);
int pause(void);
int alarm(int sec);
void sleep(unsigned sec);
int isatty(int fd);
char *ttyname(int fd);
// ttyslot() is ttyname()'s question asked again, and answers it BY calling ttyname():
// which line of /etc/ttys the terminal is, counting from 1 -- which is also its slot in
// /etc/utmp, and therefore what getlogin() indexes by.  Zero for every failure.
int ttyslot(void);
char *getlogin(void);
char *sbrk(int incr);
int brk(char *addr);

// Passwords, and the DES engine under them.  None of these four is a system call --
// crypt(), setkey() and encrypt() are lib/libc/gen/crypt.c and getpass() is
// lib/libc/stdio/getpass.c -- and they are declared here for the reason execlp() and
// execvp() are above: v7 put them in no header at all, so every caller opened with a
// prototype of its own.  There were four such copies (cmd/login, lib/test/pwent,
// lib/test/ttyt, lib/libc/gen/getlogin) and task C6 would have made nine.  crypt(3)
// documents the first three as one entry, so they arrive together; nothing in this tree
// calls setkey() or encrypt() yet.
//
// getpass() reads at most eight characters, because crypt() looks at no more.
char *crypt(const char *key, const char *salt);
void setkey(const char *key);
void encrypt(char *block, int edflag);
char *getpass(const char *prompt);

#endif // _UNISTD_H
