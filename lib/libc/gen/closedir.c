//
// closedir(dirp) -- close a directory, giving back the descriptor and both blocks.
//
// Two frees, because opendir() made two allocations; see gen/dirdesc.h for why the read
// buffer is not a fixed array inside the struct.  4.2BSD had a third thing to release
// here, the seek-point list telldir() built -- there is none here, telldir() being a plain
// byte offset on a machine whose directory entries are all the same length.
//
// It returns close(2)'s result, as POSIX asks; v7 had no such function to be compatible
// with.  The frees happen either way -- a failing close does not leave the memory
// unreclaimable.
//
#include "dirdesc.h"
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>

int closedir(DIR *dirp)
{
    int r;

    r = close(dirp->dd_fd);
    free(dirp->dd_buf);
    free(dirp);
    return r;
}
