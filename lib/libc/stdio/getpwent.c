/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * setpwent, endpwent, getpwent -- walk /etc/passwd, one entry at a time.
 *
 * One file, as v7 has it: the three share the open stream, the line buffer and the
 * struct passwd they hand back, and nothing could split them without sharing those
 * anyway.  getpwnam() and getpwuid() are next door, since a program that wants one of
 * them need not carry the other.
 *
 * The line is split IN PLACE -- each ':' becomes a '\0' and every pointer in the struct
 * aims into this buffer -- which is why <pwd.h> says the answer lasts only until the
 * next call.
 *
 * PWLINE, NOT BUFSIZ.  v7 wrote `char line[BUFSIZ+1]', where its BUFSIZ was 512 bytes.
 * BUFSIZ here is 3072 -- one 512-word disk block, which is what a stdio buffer wants to
 * be and has nothing to do with how long a passwd line is -- so spelling it BUFSIZ
 * would put 513 words of bss in this file and another 513 next door in getgrent.c.
 * 512 bytes is v7's actual size, and it is written as its own name.
 */
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>

#define PWLINE 512

static const char PASSWD[] = "/etc/passwd";
static char EMPTY[]        = "";

static FILE *pwf = NULL;
static char line[PWLINE + 1];
static struct passwd passwd;

void setpwent(void)
{
    if (pwf == NULL)
        pwf = fopen(PASSWD, "r");
    else
        rewind(pwf);
}

void endpwent(void)
{
    if (pwf != NULL) {
        fclose(pwf);
        pwf = NULL;
    }
}

/* Terminate the field p points at and return the start of the next one. */
static char *pwskip(char *p)
{
    while (*p && *p != ':')
        ++p;
    if (*p)
        *p++ = 0;
    return p;
}

struct passwd *getpwent(void)
{
    char *p;

    if (pwf == NULL) {
        if ((pwf = fopen(PASSWD, "r")) == NULL)
            return 0;
    }
    p = fgets(line, PWLINE, pwf);
    if (p == NULL)
        return 0;

    passwd.pw_name    = p;
    p                 = pwskip(p);
    passwd.pw_passwd  = p;
    p                 = pwskip(p);
    passwd.pw_uid     = atoi(p);
    p                 = pwskip(p);
    passwd.pw_gid     = atoi(p);
    passwd.pw_quota   = 0;
    passwd.pw_comment = EMPTY;
    p                 = pwskip(p);
    passwd.pw_gecos   = p;
    p                 = pwskip(p);
    passwd.pw_dir     = p;
    p                 = pwskip(p);
    passwd.pw_shell   = p;
    while (*p && *p != '\n')
        p++;
    *p = '\0';
    return &passwd;
}
