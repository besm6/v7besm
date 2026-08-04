//
// telldir(dirp) -- a cookie naming the position readdir() will read next.
//
// IT IS THE PLAIN BYTE OFFSET, and there is no machinery behind it.  4.2BSD's telldir()
// returns an index into a malloc'd list of seek points that closedir() then has to free,
// and that list exists because a BSD directory entry is variable-length: an offset alone
// cannot name one, since nothing says where the entry containing it began.  Here every
// entry is exactly DIRENTSZ bytes, so the offset IS the entry, and a cookie that is
// anything cleverer would be a cookie that can go stale.
//
// The value is meaningful across a closedir()/opendir() pair for as long as the directory
// is not written to, which is more than BSD promises and is a consequence of the same fact.
//
#include "dirdesc.h"
#include <dirent.h>
#include <sys/param.h>

long telldir(DIR *dirp)
{
    return dirp->dd_off + dirp->dd_ent * DIRENTSZ;
}
