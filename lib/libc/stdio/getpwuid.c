/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * getpwuid(uid) -- the first /etc/passwd entry with that user id, or NULL.
 *
 * The first: a uid may name more than one login, and v7 answered with whichever came
 * earlier in the file.  The answer points into getpwent()'s statics.
 */
#include <pwd.h>

struct passwd *getpwuid(int uid)
{
    struct passwd *p;

    setpwent();
    while ((p = getpwent()) != 0 && p->pw_uid != uid)
        ;
    endpwent();
    return p;
}
