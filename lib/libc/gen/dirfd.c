//
// dirfd(dirp) -- the descriptor opendir() is holding.
//
// A function rather than 4.2BSD's macro, because DIR is opaque here (see <dirent.h>).
// It is what lets a caller fstat() the directory it is walking without a second open --
// ls(1) uses it to tell a directory from the plain file it was handed.
//
// A caller must not close, lseek or read this descriptor: the DIR's buffer and offset
// would then describe a position the file no longer has.
//
#include "dirdesc.h"
#include <dirent.h>

int dirfd(DIR *dirp)
{
    return dirp->dd_fd;
}
