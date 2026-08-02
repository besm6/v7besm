/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// write -- write to another user.
//
//      write user [ttyname]
//
// Task C6, and the first program on this machine to open a terminal it does not own.
// Everything it needs was already proved: /etc/utmp has records in it (cmd/login), the
// nodes are 0622 (root.manifest), and cmd/mesg is what moves them between 0622 and 0600.
//
// IT MAKES THE PERMISSION CHECK ITSELF, and that is the thing to understand about it.
// After opening the recipient's terminal it fstat()s the descriptor and refuses on
// `(st_mode & 02) == 0' -- so a `mesg n' stops ROOT as well, even though root's open()
// succeeded and root could write the bytes with cat(1).  It is a convention between
// programs and not a kernel gate.  cmd/wall is the deliberate other half: it makes no such
// check, which is what its manual page means by the super-user overriding the protections
// a user has invoked.
//
// NOT SETUID: opening a 0622 terminal needs nothing, and a setuid write would defeat
// exactly the mesg(1) check above.
//
// THE LINE NAME IS EIGHT CHARACTERS AND NOT NUL-TERMINATED (../who/who.c is the long
// account), so v7's `strcmp(ubuf.ut_line, mytty)' read past the field into ut_time.  It is
// strncmp() over the field width here.  The name loop was already bounded, by 8, and that
// is where `me' comes from.
//
// THREE MORE FIXES:
//
//   - `read(0, buf, 128)' into `char buf[128]' and then `buf[i] = 0' on the `!' path
//     stored the terminator ONE PAST THE ARRAY whenever the line filled it.  The buffer is
//     one character longer than the read here, which is the shape ../getty/getty.c's
//     overflow took as well.
//
//   - `printf(him)' passed a caller's string as a FORMAT.  A user called %s got a walk
//     through whatever was on the stack; the same hole was closed in getpass(3)
//     (lib/libc/stdio/getpass.c).
//
//   - `strcpy(histty, "/dev/"); strcat(histty, histtya)' appended an argv string to a
//     32-byte buffer with no bound.  Bounded here.
//
// THE SIGNAL DISPOSITIONS ARE `void (*)(int)' per <signal.h>, where v7 wrote `int (*)()',
// and v7's `sigs((int (*)())0)' -- a null function pointer meaning "default" -- is spelled
// SIG_DFL.  lib/test/signals.c is the standing proof that the frame works.
//
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utmp.h>

#define LINELEN 128 // v7's read size; buf below is one longer, for the '!' terminator

static struct utmp ubuf;
static int signum[] = { SIGHUP, SIGINT, SIGQUIT, 0 };
static char me[10]  = "???";
static char *him;
static char *mytty;
static char histty[32];
static char *histtya;
static int logcnt;
static FILE *tf;

static void timout(int sig)
{
    (void)sig;
    printf("Timeout opening his tty\n");
    exit(1);
}

static void eof(int sig)
{
    (void)sig;
    fprintf(tf, "EOF\n");
    exit(0);
}

static void sigs(void (*sig)(int))
{
    int i;

    for (i = 0; signum[i]; i++)
        signal(signum[i], sig);
}

static void ex(char *bp)
{
    int i;

    sigs(SIG_IGN);
    i = fork();
    if (i < 0) {
        printf("Try again\n");
        goto out;
    }
    if (i == 0) {
        sigs(SIG_DFL);
        execl("/bin/sh", "sh", "-c", bp + 1, (char *)0);
        exit(0);
    }
    while (wait((int *)NULL) != i)
        ;
    printf("!\n");
out:
    sigs(eof);
}

int main(int argc, char *argv[])
{
    struct stat stbuf;
    char buf[LINELEN + 1];
    FILE *uf;
    int i, c1, c2;

    if (argc < 2) {
        printf("usage: write user [ttyname]\n");
        return 1;
    }
    him = argv[1];
    if (argc > 2)
        histtya = argv[2];
    if ((uf = fopen("/etc/utmp", "r")) == NULL) {
        printf("cannot open /etc/utmp\n");
        goto cont;
    }
    mytty = ttyname(2);
    if (mytty == NULL) {
        printf("Can't find your tty\n");
        return 1;
    }
    mytty = strchr(mytty + 1, '/');
    if (mytty == NULL) {
        printf("Can't find your tty\n");
        return 1;
    }
    mytty++;
    if (histtya) {
        strcpy(histty, "/dev/");
        strncat(histty, histtya, sizeof(histty) - 6);
    }
    while (fread((char *)&ubuf, sizeof(ubuf), 1, uf) == 1) {
        if (strncmp(ubuf.ut_line, mytty, sizeof(ubuf.ut_line)) == 0) {
            for (i = 0; i < 8; i++) {
                c1 = ubuf.ut_name[i];
                if (c1 == ' ')
                    c1 = 0;
                me[i] = c1;
                if (c1 == 0)
                    break;
            }
        }
        if (him[0] != '-' || him[1] != 0)
            for (i = 0; i < 8; i++) {
                c1 = him[i];
                c2 = ubuf.ut_name[i];
                if (c1 == 0)
                    if (c2 == 0 || c2 == ' ')
                        break;
                if (c1 != c2)
                    goto nomat;
            }
        logcnt++;
        if (histty[0] == 0) {
            strcpy(histty, "/dev/");
            strncat(histty, ubuf.ut_line, sizeof(ubuf.ut_line));
        }
    nomat:;
    }
cont:
    if (logcnt == 0 && histty[0] == '\0') {
        printf("%s not logged in.\n", him);
        return 1;
    }
    fclose(uf);
    if (histtya == 0 && logcnt > 1) {
        printf("%s logged more than once\nwriting to %s\n", him, histty + 5);
    }
    if (histty[0] == 0) {
        printf("%s", him);
        if (logcnt)
            printf(" not on that tty\n");
        else
            printf(" not logged in\n");
        return 1;
    }
    if (access(histty, F_OK) < 0) {
        printf("%s: ", histty);
        printf("No such tty\n");
        return 1;
    }
    signal(SIGALRM, timout);
    alarm(5);
    if ((tf = fopen(histty, "w")) == NULL)
        goto perm;
    alarm(0);
    if (fstat(fileno(tf), &stbuf) < 0)
        goto perm;
    // The mesg(1) convention, and it stops root too: the open above already succeeded.
    if ((stbuf.st_mode & 02) == 0)
        goto perm;
    sigs(eof);
    fprintf(tf, "Message from %s %s...\n", me, mytty);
    fflush(tf);
    for (;;) {
        i = read(0, buf, LINELEN);
        if (i <= 0)
            eof(0);
        if (buf[0] == '!') {
            buf[i] = 0;
            ex(buf);
            continue;
        }
        write(fileno(tf), buf, i);
    }

perm:
    printf("Permission denied\n");
    return 1;
}
