// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// The user side of the terminal interface: the three gates, over the structures and
// numbers in <sys/ttyio.h>.

#ifndef _SGTTY_H
#define _SGTTY_H

#include <sys/ttyio.h>

// The three gates the structures and commands exist for.
// All three are real syscalls here: stty and gtty are rows 31 and 32
// of sysent[], and ioctl is row 54 (kernel/dev/tty.c).
//
// ioctl's third argument is whatever the command's structure is; char * is v7's
// spelling of that and what the kernel's ioctl() reads it as.

int ioctl(int fd, int cmd, char *arg);
int stty(int fd, struct sgttyb *arg);
int gtty(int fd, struct sgttyb *arg);

#endif // _SGTTY_H
