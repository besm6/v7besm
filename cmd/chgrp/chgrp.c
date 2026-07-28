/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// chgrp -- change group.
//
// The v7 program, unchanged in what it does: turn its first argument into a gid, either by
// reading it as a decimal number or by looking it up in /etc/group with getgrnam(3), and hand
// each file named after it to chown(2) -- keeping the owner it already had.  There is no
// chgrp(2); the group is the third argument of chown(2), which is why this program and
// ../chown/chown.c are the same program with two fields exchanged.  Task C1c (../README.md).
//
// IT HAS NO MANUAL PAGE OF ITS OWN, and never had: v7 documented it inside chown(1), so
// ../chown/chown.1 is the page for both -- the arrangement ../rm/rm.1 has with rmdir.
//
// ONLY THE SUPER-USER CAN RUN IT TO ANY EFFECT, for the reason ../chown/chown.c gives at
// length: chown() in kernel/sys4.c is gated on suser(), so an ordinary user gets EPERM
// whatever the file's mode says.  chgrp is therefore NOT setuid here, and v7's was not either.
//
// The C11 pass and the four changes beyond it are ../chown/chown.c's, in full -- the deleted
// pre-ANSI `struct group *gr, *getgrnam();', isnumber() renamed out of <ctype.h>'s reserved
// namespace and taught that "" is not a number, the tested stat() (this file's copy leaves a
// stale st_UID rather than a stale st_gid, and is defensive there for the same reason), the
// owner read back and written again because chown(2) takes both ids, the same split of
// diagnostics between stdout and stderr, and the same "no long, no %D, no struct direct, no
// char * ordering -- and it was checked".  Read that file's header; this one is not going to
// say it twice.
//
#include <ctype.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static struct stat stbuf;
static int gid;
static int status;

static int numeric(const char *s)
{
    int c;

    if (*s == '\0') // v7 said yes, and atoi("") then said 0
        return 0;
    while ((c = *s++) != 0)
        if (!isdigit((unsigned char)c))
            return 0;
    return 1;
}

int main(int argc, char *argv[])
{
    struct group *gr;
    int c;

    if (argc < 3) {
        printf("usage: chgrp gid file ...\n");
        return 4;
    }
    if (numeric(argv[1])) {
        gid = atoi(argv[1]);
    } else {
        if ((gr = getgrnam(argv[1])) == NULL) {
            printf("unknown group: %s\n", argv[1]);
            return 4;
        }
        gid = gr->gr_gid;
    }

    for (c = 2; c < argc; c++) {
        // Tested, unlike v7's: a stale st_uid would hand the file to the previous argument's
        // owner.
        if (stat(argv[c], &stbuf) < 0) {
            perror(argv[c]);
            status = 1;
            continue;
        }
        if (chown(argv[c], stbuf.st_uid, gid) < 0) {
            perror(argv[c]);
            status = 1;
        }
    }
    return status;
}
