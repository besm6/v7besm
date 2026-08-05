/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// chown -- change owner.
//
// The v7 program, unchanged in what it does: turn its first argument into a uid, either by
// reading it as a decimal number or by looking it up in /etc/passwd with getpwnam(3), and hand
// each file named after it to chown(2) -- keeping the group it already had.  ../chgrp/chgrp.c
// is the same program with the two fields exchanged, and ../chown/chown.1.umm documents both, the
// way ../rm/rm.1.umm documents rmdir.  Task C1c (../README.md).
//
// ONLY THE SUPER-USER CAN RUN IT TO ANY EFFECT, and that is not a policy of this port: chown()
// in kernel/sys4.c opens with `if (!suser() || (ip = owner()) == NULL) return', so an ordinary
// user gets EPERM whatever the file's mode says.  Which is why chown is NOT setuid here --
// v7's was not either, and ../mkdir/README.md is the account of the three programs that are.
//
// The C11 pass is the usual one (../init/README.md is the worked example): a prototype and an
// explicit return type on main() and on the number test, `static' on both and on the three
// file-scope objects, the implicit-int `register c;' given its type, `while (c = *s++)'
// parenthesised, the missing <stdlib.h> and <unistd.h> added, and 4-space indentation.  main()
// returns rather than exit()s.
//
// The pre-ANSI `struct passwd *pwd, *getpwnam();' is deleted: <pwd.h> declares getpwnam
// properly here (`struct passwd *getpwnam(const char *)'), and the empty-parens form is not a
// prototype in C11 but a conflicting redeclaration.
//
// FOUR CHANGES BEYOND THE MECHANICAL PASS:
//
// 1.  isnumber() BECAME numeric().  C11 reserves every external name beginning `is' followed
//     by a lower-case letter to <ctype.h> (7.31.2), for exactly the future in which one of
//     them is added.  This tree's <ctype.h> does not declare isnumber TODAY, which is the
//     argument ../mkdir/mkdir.c makes about mkdir and reaches the same answer: rename it now
//     rather than discover the collision from a link error later.
//
// 2.  numeric("") RETURNED 1, so v7's `chown "" f' walked into atoi("") -> 0 and handed the
//     file to root without a word.  An empty string is not a number now.  It is the only way
//     a typo at this prompt could silently succeed.
//
// 3.  THE UNCHECKED stat().  v7 wrote `stat(argv[c], &stbuf);', threw the result away and
//     passed stbuf.st_gid to chown() -- which on the first argument is a plain zero and on
//     any later one is whatever the previous file left in the static.  THIS IS DEFENSIVE AND
//     NOT A VISIBLE BUG, and it is worth being exact about why: stat(2) and chown(2) both
//     resolve the path through namei(), so a stat that fails is followed by a chown that
//     fails identically, and v7's perror() then printed the right thing anyway.  What changes
//     is that a system call stops being handed a value nobody checked.  ../chgrp/chgrp.c
//     carried the mirror of it, over st_uid, and is fixed the same way.
//
// 4.  `goto cho' became the if/else that ../chgrp/chgrp.c already used for the identical
//     decision.  The two files now read the same, which is the point of them.
//
// isdigit() is given `(unsigned char)' out of habit and not necessity: plain `char' is
// UNSIGNED on this machine (../../doc/Besm6_Data_Representation.md), so it can never hand a
// <ctype.h> routine the negative value the cast exists to prevent.
//
// THE GROUP IS READ BACK AND WRITTEN AGAIN, and that is forced: chown(2) here takes all three
// arguments (kernel/sysent.c) and has no "leave this one alone" sentinel, so preserving the
// group means stat'ing it and passing it in.  ../chgrp/chgrp.c does the same with the owner.
// A test that changes both must therefore assert that neither clobbered the other.
//
// WHICH STREAM SAYS WHAT, because kernel/test/files.sh forbids a logged command from writing
// both: the usage line and "unknown user id" go to STDOUT, through printf, while a chown(2)
// that fails goes to STDERR through perror.  That split is v7's, kept here as ../cp/cp.c's and
// ../rm/rm.c's were -- a test asserting on either must redirect the one it wants alone.
//
// WHAT DID NOT NEED CHANGING, AND WAS CHECKED.  No long, no %D, no struct direct -- chown
// never reads a directory.
//
#include <ctype.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static struct stat stbuf;
static int uid;
static int status;

static int numeric(const char *s)
{
    int c;

    if (*s == '\0') // v7 said yes, and atoi("") then said 0, which is root
        return 0;
    while ((c = *s++) != 0)
        if (!isdigit((unsigned char)c))
            return 0;
    return 1;
}

int main(int argc, char *argv[])
{
    struct passwd *pwd;
    int c;

    if (argc < 3) {
        printf("usage: chown uid file ...\n");
        return 4;
    }
    if (numeric(argv[1])) {
        uid = atoi(argv[1]);
    } else {
        if ((pwd = getpwnam(argv[1])) == NULL) {
            printf("unknown user id: %s\n", argv[1]);
            return 4;
        }
        uid = pwd->pw_uid;
    }

    for (c = 2; c < argc; c++) {
        // Tested, unlike v7's: a stale st_gid would re-group the file behind the caller's back.
        if (stat(argv[c], &stbuf) < 0) {
            perror(argv[c]);
            status = 1;
            continue;
        }
        if (chown(argv[c], uid, stbuf.st_gid) < 0) {
            perror(argv[c]);
            status = 1;
        }
    }
    return status;
}
