// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// getgrnam(name) -- the /etc/group entry of that name, or NULL.
//
// A linear walk from the top of the file, as getpwnam() is.  The answer points into
// getgrent()'s statics and lasts until the next call to any of the family.
//
#include <grp.h>
#include <string.h>

struct group *getgrnam(const char *name)
{
    struct group *p;

    setgrent();
    while ((p = getgrent()) != 0 && strcmp(p->gr_name, name) != 0)
        ;
    endgrent();
    return p;
}
