/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * execvp(name, argv) -- execv(), but searching $PATH for the file.
 *
 * v7's, with its three retries intact: a file the kernel refuses as ENOEXEC is fed to
 * /bin/sh instead, one that is ETXTBSY is tried again five times, and EACCES anywhere
 * along the path is remembered so that a search which finds nothing else reports the
 * permission failure rather than the last directory's ENOENT.
 *
 * THE ETXTBSY ARM IS DEAD TODAY, twice over: no gate on this system ever returns that
 * code, and the sleep() it calls needs signal delivery to run at all -- phase 6 of
 * lib/README.md, blocked on the kernel's signal frame.  It is ported as it stands
 * because the day the kernel grows a text table is the day it starts mattering, and
 * nothing here would have to change then.
 *
 * execat() is static and shared with nothing, so execlp() lives next door in a file of
 * its own rather than here: one function per file is what lets b6ranlib's index pull
 * only what a program calls.
 *
 * The two autos are v7's sizes -- 128 bytes of name is 22 words, 256 argument slots
 * are 256 -- which the user stack based at 070000 carries without complaint.
 */
#include <errno.h>

char *getenv(const char *name);
char *index(const char *sp, char c);
int execv(const char *path, char **argv);
void sleep(unsigned n);

static char shell[] = "/bin/sh";

/*
 * Copy one path element out of s1 and the file name out of s2 into si, joined by a
 * '/' unless the element was empty (which names the working directory).  Returns the
 * start of the next element, or 0 when this was the last one.
 */
static char *execat(char *s1, const char *s2, char *si)
{
    char *s;

    s = si;
    while (*s1 && *s1 != ':' && *s1 != '-')
        *s++ = *s1++;
    if (si != s)
        *s++ = '/';
    while (*s2)
        *s++ = *s2++;
    *s = '\0';
    return *s1 ? ++s1 : 0;
}

int execvp(const char *name, char **argv)
{
    char *pathstr;
    char *cp;
    char fname[128];
    char *newargs[256];
    int i;
    unsigned etxtbsy = 1;
    int eacces       = 0;

    if ((pathstr = getenv("PATH")) == 0)
        pathstr = ":/bin:/usr/bin";
    cp = index(name, '/') ? "" : pathstr;

    do {
        cp = execat(cp, name, fname);
    retry:
        execv(fname, argv);
        switch (errno) {
        case ENOEXEC:
            newargs[0] = "sh";
            newargs[1] = fname;
            for (i = 1; (newargs[i + 1] = argv[i]) != 0; i++) {
                if (i >= 254) {
                    errno = E2BIG;
                    return -1;
                }
            }
            execv(shell, newargs);
            return -1;
        case ETXTBSY:
            if (++etxtbsy > 5)
                return -1;
            sleep(etxtbsy);
            goto retry;
        case EACCES:
            eacces++;
            break;
        case ENOMEM:
        case E2BIG:
            return -1;
        }
    } while (cp);
    if (eacces)
        errno = EACCES;
    return -1;
}
