/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// /etc/wall -- write to all users.
//
//      /etc/wall           the message is the standard input, to end-of-file
//      /etc/wall file      the message is that file
//
// Task C6.  It reads /etc/utmp the way cmd/who does and then opens every live terminal
// named in it, one forked child apiece, a second apart.
//
// IT IS /etc/wall AND NOT /bin/wall -- v7's section is 1M and its Makefile installs it in
// /etc, which is where the programs that are about the machine rather than about a file
// live (/etc/mkfs, /etc/fsck, /etc/mount).  cmd/sh's default path reaches /bin only, so it
// is typed with the path, which is v7's arrangement and not an accident.
//
// NOT SETUID, deliberately, and the manual page's own sentence is the reason: "the sender
// should be super-user to override any protections the users may have invoked."  That is a
// statement about who may IGNORE a `mesg n', and the machinery is the ordinary permission
// check on opening the terminal -- 0600 refuses everyone but its owner and root.  A setuid
// wall would hand that override to everybody, which is the protection's whole subject.
// Note the asymmetry with cmd/write, which makes the check ITSELF and therefore refuses
// even root: wall is the announcement that is allowed to be rude.
//
// TWO WILD WRITES OF v7's, both of them the same bug -- a bound that is not written down:
//
//   - `while ((i = getc(f)) != EOF) mesg[msize++] = i;' has no bound at all.  A message
//     longer than mesg[] walked off the end of the array, silently, at one byte per
//     character.  It stops at the array here and says so on the standard error, because a
//     truncated broadcast that announces itself is the only honest failure available:
//     the message has already begun going out by the time anything could refuse it.
//
//   - sendmes() built the device path with strcpy() and strcat() out of ut_line, which is
//     EIGHT CHARACTERS AND NOT NUL-TERMINATED when it is full (../who/who.c says this at
//     length).  strncat() bounded by the field width is the fix, and the buffer is sized
//     from the two lengths rather than being v7's flat 50.
//
// THE ORDER IS THE SLOT ORDER of /etc/utmp, which ttyslot() makes the line order of
// /etc/ttys: console first, tty1 second.  The sleep(1) between them is v7's and is kept --
// it staggers the writes so that two terminals sharing one operator do not print at once.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <utmp.h>

#define USERS 50

// "/dev/" plus the eight characters of ut_line plus a terminator.  Written out because
// b6cc will not take a sizeof in an array bound -- "Array size is not literal".
#define DEVNAME 14

static char mesg[3000];
static int msize;
static struct utmp utmp[USERS];

static void sendmes(const char *tty)
{
    char t[DEVNAME], buf[BUFSIZ];
    FILE *f;
    int i;

    i = fork();
    if (i == -1) {
        fprintf(stderr, "Try again\n");
        return;
    }
    if (i != 0)
        return; // the parent; the children are not waited for, as v7 left it

    strcpy(t, "/dev/");
    strncat(t, tty, sizeof(utmp[0].ut_line));

    if ((f = fopen(t, "w")) == NULL) {
        fprintf(stderr, "cannot open %s\n", t);
        exit(1);
    }
    setbuf(f, buf);
    fprintf(f, "Broadcast Message ...\n\n");
    fwrite(mesg, msize, 1, f);
    exit(0);
}

int main(int argc, char *argv[])
{
    struct utmp *p;
    FILE *f;
    int i;

    if ((f = fopen("/etc/utmp", "r")) == NULL) {
        fprintf(stderr, "Cannot open /etc/utmp\n");
        return 1;
    }
    fread((char *)utmp, sizeof(struct utmp), USERS, f);
    fclose(f);

    f = stdin;
    if (argc >= 2) {
        if ((f = fopen(argv[1], "r")) == NULL) {
            fprintf(stderr, "Cannot open %s\n", argv[1]);
            return 1;
        }
    }
    while ((i = getc(f)) != EOF) {
        if (msize >= (int)sizeof(mesg)) {
            fprintf(stderr, "wall: message truncated at %d characters\n", msize);
            break;
        }
        mesg[msize++] = i;
    }
    fclose(f);

    for (i = 0; i < USERS; i++) {
        p = &utmp[i];
        if (p->ut_name[0] == '\0')
            continue;
        sleep(1);
        sendmes(p->ut_line);
    }
    return 0;
}
