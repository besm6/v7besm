// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// getgrgid(gid) -- the first /etc/group entry with that group id, or NULL.
//
#include <grp.h>

struct group *getgrgid(int gid)
{
    struct group *p;

    setgrent();
    while ((p = getgrent()) != 0 && p->gr_gid != gid)
        ;
    endgrent();
    return p;
}
