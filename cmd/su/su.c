/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// su -- become another user.
//
//      su          become root
//      su name     become that user
//
// Task C6, and one of the two programs on this image that are SETUID ROOT for a reason
// other than a directory operation.  cmd/login/README.md named this program a task in
// advance: v7's login(1) page claims login "may be used at any time to change from one user
// to another", that use needs the setuid bit, and the command for it is this one.
//
// THE SETUID BIT LIVES IN root.manifest AND NOWHERE ELSE (../README.md §7, §8): nothing in
// build/rootfs/ carries a mode, `mode 04755' reaches the inode as IFREG | (mode & 07777),
// and getxfile() (kernel/sys1.c) takes the ISUID branch only `if (u.u_uid != 0)'.  So the
// program is privileged when a user runs it and unprivileged when root does, which is
// exactly the shape v7's code assumes below.
//
// getuid() IS THE REAL UID AND THAT IS WHAT MAKES `getuid() == 0' CORRECT.  The bit has
// already set the EFFECTIVE uid to 0 by the time main() runs; the test asks whether the
// person who typed the command was root, and only the real uid can answer that.
//
// setuid() SETS BOTH IDS AND THERE IS NO WAY BACK (kernel/sys4.c): it is allowed when
// u_ruid matches or suser() passes, and it moves u_ruid too.  So the sequence at `ok' is
// one-way by construction, which is the property the program exists to have -- a shell that
// could climb back would be no protection at all.
//
// THE PROMPT IS getpass()'s AND GOES TO STANDARD ERROR, so cmd/login/README.md's finding
// about a line-buffered prompt with no newline does not bite here.  Every printf below ends
// in a newline.
//
// THE DIFF FROM v7 IS THE C11 PASS AND ONE LINE OF IT MATTERS: the K&R declarations at the
// head had to GO rather than be modernised -- <pwd.h> declares getpwnam()/endpwent(),
// <unistd.h> declares environ and, since task C6a, crypt() and getpass() -- and a second
// declaration of a different shape is an error, not a redundancy.  execl()'s terminator is
// `(char *)0' and not a bare 0: a char * is a fat pointer here (../README.md §2).
//
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    struct passwd *pwd;
    char **p;
    char *nptr;
    char *password;
    char *shell = "/bin/sh";

    if (argc > 1)
        nptr = argv[1];
    else
        nptr = "root";
    if ((pwd = getpwnam(nptr)) == NULL) {
        printf("Unknown id: %s\n", nptr);
        return 1;
    }
    if (pwd->pw_passwd[0] == '\0' || getuid() == 0)
        goto ok;
    password = getpass("Password:");
    if (strcmp(pwd->pw_passwd, crypt(password, pwd->pw_passwd)) != 0) {
        printf("Sorry\n");
        return 2;
    }

ok:
    endpwent();
    setgid(pwd->pw_gid);
    setuid(pwd->pw_uid);
    if (pwd->pw_shell && *pwd->pw_shell)
        shell = pwd->pw_shell;
    for (p = environ; *p; p++) {
        if (strncmp("PS1=", *p, 4) == 0) {
            *p = "PS1=# ";
            break;
        }
    }
    execl(shell, "su", (char *)0);
    printf("No shell\n");
    return 3;
}
