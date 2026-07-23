// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// <pwd.h> -- the password file, /etc/passwd (getpwent(3)).
//
// v7's header declared the struct and nothing else, leaving the five routines to
// K&R's implicit int; there is no implicit anything now, and this header exists
// for those routines alone, so it names them.  It is the deliberate exception to
// lib/README.md's rule that a routine no v7 header declares declares itself at
// its own file head: <pwd.h> is not a header added to declare a function, it is
// the header these functions were always part of.
//
// pw_quota and pw_comment are v7 fields that /etc/passwd has no columns for;
// getpwent() sets them to 0 and "" respectively, as v7's did.
//
// EVERY POINTER HERE AIMS INTO ONE SHARED LINE BUFFER, and so does the struct
// itself: the next getpwent() overwrites both.  A caller that wants to keep an
// entry copies the strings out.  That is v7's contract and it has not changed.

#ifndef _PWD_H
#define _PWD_H

struct passwd { // see getpwent(3)
    char *pw_name;
    char *pw_passwd;
    int pw_uid;
    int pw_gid;
    int pw_quota;
    char *pw_comment;
    char *pw_gecos;
    char *pw_dir;
    char *pw_shell;
};

struct passwd *getpwent(void);
struct passwd *getpwuid(int uid);
struct passwd *getpwnam(const char *name);
void setpwent(void);
void endpwent(void);

#endif // _PWD_H
