//
// seekdir(dirp, loc) -- go back to a position telldir() returned.
//
// loc is a byte offset (see telldir.c), so this is an lseek(2) and a reset -- except when
// the position is still inside the buffer already read, which is the common case for a
// caller that marked a spot and read a few entries past it.  Then it is one subtraction
// and no system call at all.
//
// LOC IS ROUNDED DOWN to a whole entry.  One divide, and it turns a caller's arithmetic
// slip from a straddled read of garbage into defined behaviour -- the position it names is
// the entry it lands in.  Nothing else can round it: read() would happily deliver a buffer
// starting mid-entry and every field in it would be nonsense.
//
#include "dirdesc.h"
#include <dirent.h>
#include <sys/param.h>
#include <unistd.h>

void seekdir(DIR *dirp, long loc)
{
    long base;

    loc -= loc % DIRENTSZ;
    if (loc < 0)
        loc = 0;

    base = dirp->dd_off;
    if (loc >= base && loc < base + (long)(dirp->dd_nent * DIRENTSZ)) {
        dirp->dd_ent = (int)((loc - base) / DIRENTSZ);
        return;
    }

    lseek(dirp->dd_fd, loc, SEEK_SET);
    dirp->dd_off  = loc;
    dirp->dd_nent = 0;
    dirp->dd_ent  = 0;
}
