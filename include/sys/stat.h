// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// <sys/types.h> is included rather than assumed, for the reason <sys/timeb.h>
// spells out: dev_t, ino_t, off_t and time_t all come from there, and this file
// sorts ahead of it in an include list clang-format has put in order.

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

#endif // _SYS_STAT_H
