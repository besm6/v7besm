/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// ln -- make a link.
//
// The v7 program, unchanged in what it does: find the last component of the target's path,
// use it as the new name if none was given, refuse a directory unless -f, and make one
// link(2).  The smallest of task C1b's four (../README.md).
//
// The C11 pass is the usual one (../init/README.md is the worked example): a prototype and an
// explicit return type on main(), the pre-ANSI `char *rindex();' deleted, `#include "stdio.h"'
// spelled with angle brackets -- it is a system header, not a local one -- and the missing
// <string.h>, <stdlib.h> and <unistd.h> added.  main() returns rather than exit()s.
//
// `rindex' became strrchr(), for the reason ../rmdir/rmdir.c gives: rindex IS in this libc
// (lib/libc/gen/rindex.c) but NO HEADER DECLARES IT, since it is not ANSI and this tree has no
// <strings.h>, while strrchr is in <string.h>.
//
// ONE CHANGE BEYOND THE MECHANICAL PASS: the sprintf() that builds "directory/name" was
// unbounded into a 100-byte automatic, and both of its operands come from argv.  That is the
// hazard ../mkdir/mkdir.c and ../rmdir/rmdir.c each bounded in their turn -- the 4,096-word
// stack at 070000 is the one address-space ceiling nothing checks (../README.md, the porting
// recipe).  One length test, and ln.1.umm says so.
//
// WHAT DID NOT NEED CHANGING, AND WAS CHECKED.  No long, no %D, no struct direct -- ln never
// reads a directory.
//
// THE -f FLAG IS THE INTERESTING ONE ON THIS MACHINE, and ln.1.umm had never documented it at all.
// With -f, ln will hand a directory to link(2), and this kernel allows that for the
// super-user alone -- `if ((ip->i_mode & IFMT) == IFDIR && !suser())' in link(),
// kernel/sys2.c.  So the v7 page's flat "it is forbidden to link to a directory" is true of an
// ordinary user and false of root, and root doing it makes a cycle that only b6fsutil -c will
// complain about.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// v7's own 100, kept: 17 words of the 4,096-word stack, and the bound below is what makes the
// number mean anything.
#define NAMEBUF 100

int main(int argc, char **argv)
{
    struct stat statb;
    char *np;
    int fflag = 0;
    char nb[NAMEBUF], *name = nb, *arg2;
    int statres;

    if (argc > 1 && strcmp(argv[1], "-f") == 0) {
        argc--;
        argv++;
        fflag++;
    }
    if (argc < 2 || argc > 3) {
        printf("Usage: ln [ -f ] target [ newname ]\n");
        return 1;
    }

    // The last component of the target's path, which is the default new name.
    np = strrchr(argv[1], '/');
    if (np == NULL)
        np = argv[1];
    else
        np++;

    if (argc == 2)
        arg2 = np;
    else
        arg2 = argv[2];
    statres = stat(argv[1], &statb);
    if (statres < 0) {
        printf("ln: %s does not exist\n", argv[1]);
        return 1;
    }
    if (fflag == 0 && (statb.st_mode & S_IFMT) == S_IFDIR) {
        printf("ln: %s is a directory\n", argv[1]);
        return 1;
    }
    statres = stat(arg2, &statb);
    if (statres >= 0 && (statb.st_mode & S_IFMT) == S_IFDIR) {
        // The bound v7 had not: nb grows to arg2 + '/' + np + a NUL.
        if ((int)strlen(arg2) + (int)strlen(np) + 2 > (int)sizeof nb) {
            printf("ln: %s/%s: name too long\n", arg2, np);
            return 1;
        }
        sprintf(name, "%s/%s", arg2, np);
    } else
        name = arg2;
    if (link(argv[1], name) < 0) {
        perror("ln");
        return 1;
    }
    return 0;
}
