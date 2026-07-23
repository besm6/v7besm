/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * getpwnam(name) -- the /etc/passwd entry for a login name, or NULL.
 *
 * A linear walk, as v7 wrote it, and it rewinds the file each time: there is no index
 * on /etc/passwd and never was.  The answer points into getpwent()'s statics and lasts
 * only until the next call to any of the family.
 */
#include <pwd.h>
#include <string.h>

struct passwd *getpwnam(const char *name)
{
    struct passwd *p;

    setpwent();
    while ((p = getpwent()) != 0 && strcmp(name, p->pw_name) != 0)
        ;
    endpwent();
    return p;
}
