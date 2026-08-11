/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "defs.h"

static int metas(char *s);
static int doshell(char *comstring, int nohalt);
static int doexec(char *str);
static int await(void);

int dosys(char *comstring, int nohalt)
{
    int status;

    if (metas(comstring))
        status = doshell(comstring, nohalt);
    else
        status = doexec(comstring);

    return status;
}

// Are there any Shell meta-characters?  funny['\0'] carries META, so the NUL
// ends the loop and its own value is the answer.
static int metas(char *s)
{
    char c;

    while ((funny[(unsigned char)(c = *s++)] & META) == 0)
        ;
    return c;
}

static int doshell(char *comstring, int nohalt)
{
    if ((childpid = fork()) == 0) {
        enbint(SIG_DFL);
        doclose();

        execl(SHELLCOM, "sh", (nohalt ? "-c" : "-ce"), comstring, (char *)NULL);
        // _exit(), not fatal(): this is a copy of the parent, and exit() would
        // flush the parent's stdio buffers a second time.
        fprintf(stderr, "Make: Couldn't load Shell.  Stop.\n");
        fflush(stderr);
        _exit(1);
    }
    if (childpid < 0)
        fatal("Cannot fork");

    return await();
}

static int await(void)
{
    int status;
    int pid;

    enbint(SIG_IGN);
    // wait(2) names no child and there is no waitpid() here, so take deaths
    // until this one's comes back -- cmd/cc/cc.c's run() does the same.
    while ((pid = wait(&status)) != childpid)
        if (pid == -1) {
            if (errno == EINTR)
                continue;
            fatal("bad wait code");
        }
    childpid = 0;
    enbint(intrupt);
    return status;
}

// Close open directories before exec'ing
void doclose(void)
{
    struct opendir *od;

    for (od = firstod; od; od = od->nxtopendir)
        if (od->dirfc != NULL)
            closedir(od->dirfc);
}

static int doexec(char *str)
{
    char *t;
    char *argv[200];
    char **p;

    while (*str == ' ' || *str == '\t')
        ++str;
    if (*str == '\0')
        return -1; // no command

    p = argv;
    for (t = str; *t;) {
        if (p >= &argv[sizeof(argv) / sizeof(argv[0]) - 1]) {
            fprintf(stderr, "Too many arguments\n");
            return -1;
        }
        *p++ = t;
        while (*t != ' ' && *t != '\t' && *t != '\0')
            ++t;
        if (*t)
            for (*t++ = '\0'; *t == ' ' || *t == '\t'; ++t)
                ;
    }

    *p = NULL;

    if ((childpid = fork()) == 0) {
        enbint(SIG_DFL);
        doclose();
        enbint(intrupt);
        execvp(str, argv);
        fprintf(stderr, "Make: Cannot load %s.  Stop.\n", str);
        fflush(stderr);
        _exit(1);
    }
    if (childpid < 0)
        fatal("Cannot fork");

    return await();
}

void touch(int force, char *name)
{
    struct stat stbuff;
    char junk[1];
    int fd;

    if (stat(name, &stbuff) < 0) {
        if (force)
            goto create;
        else {
            fprintf(stderr, "touch: file %s does not exist.\n", name);
            return;
        }
    }

    if (stbuff.st_size == 0)
        goto create;

    if ((fd = open(name, 2)) < 0)
        goto bad;

    if (read(fd, junk, 1) < 1) {
        close(fd);
        goto bad;
    }
    lseek(fd, 0L, 0);
    if (write(fd, junk, 1) < 1) {
        close(fd);
        goto bad;
    }
    close(fd);
    return;

bad:
    fprintf(stderr, "Cannot touch %s\n", name);
    return;

create:
    if ((fd = creat(name, 0666)) < 0)
        goto bad;
    close(fd);
}
