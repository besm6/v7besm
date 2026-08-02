/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// passwd -- change a login password.
//
//      passwd          change your own, named by getlogin()
//      passwd user     change that user's; root may change anybody's
//
// Task C6, and the only program in it that WRITES /etc/passwd.  SETUID ROOT, which is the
// point: the file is 0644 and root's, so nothing else could put a new hash in it, and this
// is one of the two programs cmd/login/README.md said the bit login deliberately does not
// carry would have to go to.
//
// HOW IT REWRITES THE FILE, and there is no rename(2) in this kernel to do it the modern
// way: /etc/ptmp is created 0600, the whole password file is copied through getpwent() with
// the matching line's hash replaced, and then the temp file is copied BACK over /etc/passwd
// with creat().  The window is real -- a crash between the creat() and the last write
// leaves a short password file -- and it is v7's design, not something this port chose.
// What it does buy is that /etc/passwd keeps its inode, so nothing holding it open sees the
// file vanish.
//
// /etc/ptmp IS THE LOCK, and it leaks.  access(temp, F_OK) is the test for "somebody else
// is doing this", and every failure path AFTER the creat() jumps to `bex', which does not
// unlink it -- so an interrupted passwd leaves a file that makes every later run say
// `Temporary file busy'.  Only the paths that reach `out' clean up.  That is v7's and it is
// left alone deliberately: the alternative is unlinking a file this process may not have
// created, which is worse.  passwd.1 records it.
//
// getlogin() IS NOT getpwuid(getuid()), and the difference is the whole reason the no-
// argument form works: getlogin() reads the /etc/utmp record for THIS TERMINAL
// (lib/libc/gen/getlogin.c, indexing by ttyslot()), so a user who has su'd is still asked
// about the account they logged in as.  Under this port that record exists because
// cmd/login writes it -- before task 29b there was nothing for this path to read.
//
// THE SALT IS time() + getpid(), AND time_t IS ONE WORD.  v7 declared `long salt' and this
// machine's long, int and time_t are the same 41-bit word (../README.md §3), so the
// declaration is time_t and the `& 077' and `>> 6' that cut the two salt characters out of
// it are unchanged.  crypt(3) is libc's and lib/test/pwent.c pins six of its vectors
// against the host's DES, so a hash written here is one the host could have written.
//
// TWO FIXES:
//
//   - v7 EXITED 1 ON SUCCESS.  Every path, including the one that rewrote the file, fell
//     into `bex: exit(1)'.  A shell script could not tell a changed password from a refused
//     one.  Success is 0 here and passwd.1 says so.
//
//   - the signal dispositions are `void (*)(int)' per <signal.h>, where v7 wrote the K&R
//     form.  They are set BEFORE the temp file is created, which is v7's order and is the
//     right one: an interrupt after the creat() is exactly the case that leaks /etc/ptmp.
//
// THE PROMPTS ARE getpass()'s and go to standard error, so cmd/login/README.md's
// line-buffered-prompt finding does not bite; the one bare printf that has no newline is
// not a prompt.
//
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static char passwd[] = "/etc/passwd";
static char temp[]   = "/etc/ptmp";
static struct passwd *pwd;
static char *pw;
static char pwbuf[10];
static char buf[512];

int main(int argc, char *argv[])
{
    char *p;
    int i;
    char saltc[2];
    time_t salt;
    int u, fi, fo;
    int insist;
    int ok, flags;
    int c;
    int pwlen;
    FILE *tf;
    char *uname;
    int status = 1;

    insist = 0;
    if (argc < 2) {
        if ((uname = getlogin()) == NULL) {
            printf("Usage: passwd user\n");
            goto bex;
        } else {
            printf("Changing password for %s\n", uname);
        }
    } else {
        uname = argv[1];
    }
    while (((pwd = getpwent()) != NULL) && (strcmp(pwd->pw_name, uname) != 0))
        ;
    u = getuid();
    if ((pwd == NULL) || (u != 0 && u != pwd->pw_uid)) {
        printf("Permission denied.\n");
        goto bex;
    }
    endpwent();
    if (pwd->pw_passwd[0] && u != 0) {
        strcpy(pwbuf, getpass("Old password:"));
        pw = crypt(pwbuf, pwd->pw_passwd);
        if (strcmp(pw, pwd->pw_passwd) != 0) {
            printf("Sorry.\n");
            goto bex;
        }
    }
tryagn:
    strcpy(pwbuf, getpass("New password:"));
    pwlen = strlen(pwbuf);
    if (pwlen == 0) {
        printf("Password unchanged.\n");
        goto bex;
    }
    ok    = 0;
    flags = 0;
    p     = pwbuf;
    while ((c = *p++)) {
        if (c >= 'a' && c <= 'z')
            flags |= 2;
        else if (c >= 'A' && c <= 'Z')
            flags |= 4;
        else if (c >= '0' && c <= '9')
            flags |= 1;
        else
            flags |= 8;
    }
    if (flags >= 7 && pwlen >= 4)
        ok = 1;
    if (((flags == 2) || (flags == 4)) && pwlen >= 6)
        ok = 1;
    if (((flags == 3) || (flags == 5) || (flags == 6)) && pwlen >= 5)
        ok = 1;

    if ((ok == 0) && (insist < 2)) {
        if (flags == 1)
            printf("Please use at least one non-numeric character.\n");
        else
            printf("Please use a longer password.\n");
        insist++;
        goto tryagn;
    }

    if (strcmp(pwbuf, getpass("Retype new password:")) != 0) {
        printf("Mismatch - password unchanged.\n");
        goto bex;
    }

    time(&salt);
    salt += getpid();

    saltc[0] = salt & 077;
    saltc[1] = (salt >> 6) & 077;
    for (i = 0; i < 2; i++) {
        c = saltc[i] + '.';
        if (c > '9')
            c += 7;
        if (c > 'Z')
            c += 6;
        saltc[i] = c;
    }
    pw = crypt(pwbuf, saltc);
    signal(SIGHUP, SIG_IGN);
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);

    if (access(temp, F_OK) >= 0) {
        printf("Temporary file busy -- try again\n");
        goto bex;
    }
    close(creat(temp, 0600));
    if ((tf = fopen(temp, "w")) == NULL) {
        printf("Cannot create temporary file\n");
        goto bex;
    }

    //
    // Copy passwd to temp, replacing matching lines with the new password.
    //
    while ((pwd = getpwent()) != NULL) {
        if (strcmp(pwd->pw_name, uname) == 0) {
            u = getuid();
            if (u != 0 && u != pwd->pw_uid) {
                printf("Permission denied.\n");
                goto out;
            }
            pwd->pw_passwd = pw;
        }
        fprintf(tf, "%s:%s:%d:%d:%s:%s:%s\n", pwd->pw_name, pwd->pw_passwd, pwd->pw_uid,
                pwd->pw_gid, pwd->pw_gecos, pwd->pw_dir, pwd->pw_shell);
    }
    endpwent();
    fclose(tf);

    //
    // Copy temp back to the password file.
    //
    if ((fi = open(temp, O_RDONLY)) < 0) {
        printf("Temp file disappeared!\n");
        goto out;
    }
    if ((fo = creat(passwd, 0644)) < 0) {
        printf("Cannot recreat passwd file.\n");
        goto out;
    }
    while ((u = read(fi, buf, sizeof(buf))) > 0)
        write(fo, buf, u);
    status = 0;

out:
    unlink(temp);

bex:
    return status;
}
