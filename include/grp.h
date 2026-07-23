// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// <grp.h> -- the group file, /etc/group (getgrent(3)).
//
// The same shape as <pwd.h>, and named here for the same reason: this header
// exists for these five routines, so it declares them.
//
// gr_mem is a NULL-terminated vector of member names, and it too is a static of
// getgrent()'s -- the struct, the strings and the vector are all overwritten by
// the next call.

#ifndef _GRP_H
#define _GRP_H

struct group { // see getgrent(3)
    char *gr_name;
    char *gr_passwd;
    int gr_gid;
    char **gr_mem;
};

struct group *getgrent(void);
struct group *getgrgid(int gid);
struct group *getgrnam(const char *name);
void setgrent(void);
void endgrent(void);

#endif // _GRP_H
