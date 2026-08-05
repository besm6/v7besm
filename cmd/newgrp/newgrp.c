/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// newgrp -- change to a new group.
//
//      newgrp groupname
//
// Task C6, and the third of the account trio.  It replaces the caller's shell with one
// whose group is the named group, so that files created afterwards belong to it.
//
// SETUID ROOT, and this is the one of the three whose reason is not obvious.  setgid(2)
// (kernel/sys4.c) is allowed when `u_rgid == gid' or suser() passes -- and the whole point
// of the program is to move to a group that is NOT the caller's, so the second gate is the
// only one available.  done() gives the privilege straight back with setuid(getuid()),
// which is the saved-uid-free dance every v7 program of this shape does: setuid() moves the
// real uid too (there is no way back afterwards), so it has to be the last privileged act.
//
// THE MEMBERSHIP TEST IS THE PROGRAM.  /etc/group lists the members of each group and the
// caller must be among them -- except for the group literally named `other', which v7
// exempts and which this image's group file has as gid 1.  A group password is asked for
// only when the caller has NO password of their own, which is v7's rule and reads backwards
// until you notice that it is about accounts that anyone may use.
//
// WHAT IT DOES ON FAILURE IS NOT WHAT IT LOOKS LIKE: every path, including every refusal,
// ends in done(), which execs a shell.  `newgrp nosuchgroup' prints its complaint and then
// hands back a shell with the group UNCHANGED rather than exiting -- because the shell it
// replaced is gone and leaving the terminal without one would log the user out.  That is v7
// and it is deliberate; newgrp.1.umm now says so, since the exit status cannot.
//
// NOFILE RATHER THAN 15.  v7 closed descriptors 3 through 14 because NOFILE was 15 there;
// it is 20 here (<sys/param.h>), and the loop is written from the constant so that a kernel
// that retunes it does not leave three descriptors open across the exec.
//
// THE DIFF FROM v7 IS OTHERWISE THE C11 PASS.  The K&R declarations at the head had to GO
// rather than be modernised: <grp.h> and <pwd.h> declare their families and <unistd.h>
// declares crypt() and getpass() since task C6a.  execl()'s terminator is `(char *)0' and
// not a bare 0 -- a char * is a fat pointer here (../README.md §2).
//
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <unistd.h>

static void done(void)
{
    int i;

    setuid(getuid());
    for (i = 3; i < NOFILE; i++)
        close(i);
    execl("/bin/sh", "sh", (char *)0);
    printf("No shell!\n");
    exit(0);
}

int main(int argc, char *argv[])
{
    struct group *grp;
    struct passwd *pwd;
    int i;

    if (argc != 2) {
        printf("usage: newgrp groupname\n");
        done();
    }
    if ((grp = getgrnam(argv[1])) == NULL) {
        printf("%s: no such group\n", argv[1]);
        done();
    }
    if ((pwd = getpwuid(getuid())) == NULL) {
        printf("You do not exist!\n");
        done();
    }
    for (i = 0; grp->gr_mem[i]; i++)
        if (strcmp(grp->gr_mem[i], pwd->pw_name) == 0)
            break;
    if (grp->gr_mem[i] == 0 && strcmp(grp->gr_name, "other")) {
        printf("Sorry\n");
        done();
    }

    if (grp->gr_passwd[0] != '\0' && pwd->pw_passwd[0] == '\0') {
        if (strcmp(grp->gr_passwd, crypt(getpass("Password:"), grp->gr_passwd)) != 0) {
            printf("Sorry\n");
            done();
        }
    }
    if (setgid(grp->gr_gid) < 0)
        perror("setgid");
    done();
    return 0;
}
