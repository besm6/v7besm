//
// readdir(dirp) -- the next entry, or NULL at the end of the directory.
//
// The returned struct is the DIR's own and the next call overwrites it, which is
// getpwent(3)'s contract and for the same reason: a caller that wants to keep a name
// copies it out.  ls(1) does, into a strdup.
//
// IT SKIPS THE FREE SLOTS, and that is the contract rather than an optimisation.
// unlink(2) empties an entry by writing d_ino = 0 back in place (kernel/sys4.c), so a
// hole sits in the MIDDLE of a live directory and every open-coded caller in this tree
// tests for it by hand (gen/ttyname.c, cmd/ls, cmd/rm, cmd/pwd).  A readdir() that
// returned holes would buy nobody anything.
//
// AND IT TERMINATES THE NAME, which is the other half of why this library exists.  On the
// disk d_name is DIRSIZ characters with no room for a NUL when a name fills the field --
// so d_name[DIRSIZ] is not a terminator, it is THE NEXT ENTRY'S d_ino.  A strlen() there
// is the exact bug include/sys/dir.h and cmd/README.md §5 warn about; memchr() bounded by
// DIRSIZ is the answer, and the length it finds is worth keeping (d_namlen), since every
// caller wants it and the scan has already been paid for.
//
// A short name IS zero-padded on the disk -- wdir() copies the zero-filled u_dbuf
// (kernel/iget.c) -- so the memchr usually stops early.  Only a full-width name has no NUL
// at all, and that is the case the +1 exists for.
//
#include "dirdesc.h"
#include <dirent.h>
#include <string.h>
#include <sys/dir.h>
#include <sys/param.h>
#include <sys/types.h>
#include <unistd.h>

struct dirent *readdir(DIR *dirp)
{
    struct direct *dp;
    char *ep;
    int n;

    for (;;) {
        if (dirp->dd_ent >= dirp->dd_nent) {
            // The buffer is emptied BEFORE the read, not after a successful one, so
            // that end-of-directory is a fixed point: dd_off names the end, dd_nent is
            // zero, and reading past the end again changes nothing.  Advancing dd_off
            // on the way out instead would let telldir() drift one buffer per call for
            // a caller that keeps asking after NULL.
            dirp->dd_off += dirp->dd_nent * DIRENTSZ;
            dirp->dd_ent  = 0;
            dirp->dd_nent = 0;

            n = read(dirp->dd_fd, (char *)dirp->dd_buf, dirp->dd_max * DIRENTSZ);
            if (n <= 0)
                return NULL;

            // A short read is the last block of the directory; any remainder past
            // the last whole entry is a corrupt one, and is dropped rather than
            // reinterpreted.
            dirp->dd_nent = n / DIRENTSZ;
            if (dirp->dd_nent == 0)
                return NULL;
        }

        dp = &dirp->dd_buf[dirp->dd_ent++];
        if (dp->d_ino == 0)
            continue;

        ep = memchr(dp->d_name, '\0', DIRSIZ);
        n  = (ep == NULL) ? DIRSIZ : (int)(ep - dp->d_name);
        memcpy(dirp->dd_ret.d_name, dp->d_name, n);
        dirp->dd_ret.d_name[n] = '\0';
        dirp->dd_ret.d_namlen  = n;
        dirp->dd_ret.d_ino     = dp->d_ino;
        return &dirp->dd_ret;
    }
}
