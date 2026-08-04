//
// opendir(name) -- open a directory for reading, or NULL.
//
// v7's callers did the open(2) themselves and asked nothing about what they had opened.
// THIS ONE REFUSES A NON-DIRECTORY, and it has to: read(2) on a plain file succeeds here,
// so without the test readdir() would hand back file contents reinterpreted as entries --
// silently, since a `struct direct' has no field that can be checked for sense.  4.2BSD's
// opendir() did not have this test either, its read() on a directory being the only one
// the kernel allowed.
//
// The fstat() pays for itself twice: st_mode is that test, and st_size sizes the buffer.
//
// TWO ALLOCATIONS, and the second is the one that matters.  The DIR itself is twelve
// words; the read buffer is as many entries as the directory holds TODAY, capped at DIRPB
// -- which is exactly one filesystem block, so a full read is one buffer-cache block and
// no entry straddles the buffer.  A directory that grows while it is open just costs
// another read(2).  See gen/dirdesc.h for why it is not a fixed array inside the struct.
//
#include "dirdesc.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/dir.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

DIR *opendir(const char *name)
{
    struct stat sb;
    DIR *dirp;
    int fd, n;

    if ((fd = open(name, O_RDONLY)) < 0)
        return NULL;

    // Two arms, not one: ENOTDIR must not overwrite whatever fstat() failed with.
    if (fstat(fd, &sb) < 0) {
        close(fd);
        return NULL;
    }
    if ((sb.st_mode & S_IFMT) != S_IFDIR) {
        close(fd);
        errno = ENOTDIR; // after the close, which may set errno of its own
        return NULL;
    }

    n = (sb.st_size + DIRENTSZ - 1) / DIRENTSZ;
    if (n > DIRPB)
        n = DIRPB;
    if (n < 1)
        n = 1;

    dirp = (DIR *)malloc(sizeof(DIR));
    if (dirp == NULL) {
        close(fd);
        return NULL;
    }
    dirp->dd_buf = (struct direct *)malloc(n * DIRENTSZ);
    if (dirp->dd_buf == NULL) {
        free(dirp);
        close(fd);
        return NULL;
    }

    dirp->dd_fd   = fd;
    dirp->dd_ent  = 0;
    dirp->dd_nent = 0;
    dirp->dd_max  = n;
    dirp->dd_off  = 0;
    return dirp;
}
