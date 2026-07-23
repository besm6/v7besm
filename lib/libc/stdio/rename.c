//
// rename -- give a file a new name (C11 §7.21.4.2).  Not a v7 routine, and this
// kernel has no rename syscall: it is link() followed by unlink(), which is what
// mv(1) does and carries mv's limits with it.  The new name must not already
// exist -- link() refuses -- and a directory cannot be renamed at all, since only
// the super-user may link one (kernel/sys4.c).
//
#include <stdio.h>

int link(const char *target, const char *linkname);
int unlink(const char *path);

int rename(const char *from, const char *to)
{
    if (link(from, to) < 0)
        return -1;
    if (unlink(from) < 0)
        return -1;
    return 0;
}
