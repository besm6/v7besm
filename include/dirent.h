// <dirent.h> -- reading a directory, POSIX's way rather than by hand.
//
// v7 had no such library at all: every caller open(2)ed the directory as a file and
// read(2) raw `struct direct' records out of it, and eleven programs in cmd/ each grew
// their own copy of the same four mistakes -- forgetting the free slots, running a
// strlen() off the end of the name field, forgetting the terminator that is not there,
// and re-deriving DIRENTSZ.  This header is where that stops, and since task C24 there
// is no other reader of a pathname in the tree.  <sys/dir.h> is for a program that has a
// disk block rather than a directory: fsck, mkfs, ncheck, dcheck, pstat.
//
// THIS IS NOT THE ON-DISK ENTRY, and the difference is the whole point.  <sys/dir.h>'s
// `struct direct' is the format ON THE DISK: exactly four words, DIRPB of them tiling a
// block, and d_name being DIRSIZ characters with NO ROOM for a terminator when a name
// fills the field.  `struct dirent' below has that terminator and readdir() plants it.
// A struct dirent is never read() into and never tiles anything; nothing about it is on
// any disk.  The two structs coexist by design -- lib/libc/gen/readdir.c includes both
// headers -- so do NOT typedef or #define either to the other.
//
// It is deliberately NOT under sys/.  Thirty-odd kernel sources include <sys/dir.h> (and
// <sys/user.h> pulls it in for u_dent), the kernel's header dependency is the whole of
// include/sys/ at once, and none of the kernel wants a user-space library declaration.
//
// DIRSIZ has one home, <sys/param.h>, and this file reads it there rather than restating
// it: b6cpp rejects a macro redefinition whose replacement text is not character-identical,
// and the one way to be safe from that is to define no macro whatever.  This header
// defines none.
//
// The whole family is lib/libc/gen/{opendir,readdir,closedir,rewinddir,telldir,seekdir,
// dirfd}.c and directory(3); struct _dirdesc is private to them.

#ifndef _DIRENT_H
#define _DIRENT_H

#include <sys/param.h> // DIRSIZ, NBPW -- included, not assumed, as sys/dir.h explains
#include <sys/types.h> // ino_t

struct dirent {
    ino_t d_ino;              // never 0: readdir() skips the slots unlink() emptied
    int d_namlen;             // strlen(d_name), which readdir() computed on the way past
    char d_name[DIRSIZ + 1];  // NUL-terminated, unlike the field on the disk
};

// Six words.  Asserted because one of these lives inside every open DIR, so a member
// added here is a member added to every directory a program has open at once.
_Static_assert(sizeof(struct dirent) == 6 * NBPW, "struct dirent must be six words");

// OPAQUE, and meant to stay so.  lib/libc/gen/dirdesc.h defines it; nothing outside libc
// needs to know how the read buffer is sized, and publishing the struct would freeze that
// choice into every caller.  It would also let a program declare a DIR as an automatic on
// a 4096-word stack that nothing checks.  dirfd() is a function for the same reason --
// there is no getc()-shaped argument for a macro here, readdir() not being one either.
typedef struct _dirdesc DIR;

DIR *opendir(const char *name);
struct dirent *readdir(DIR *dirp);
int closedir(DIR *dirp);
void rewinddir(DIR *dirp);
long telldir(DIR *dirp);
void seekdir(DIR *dirp, long loc);
int dirfd(DIR *dirp);

#endif // _DIRENT_H
