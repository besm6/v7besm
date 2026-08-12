/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// /usr/lib/atrun -- run the scripts at(1) spooled, once they are due.  Task C21; the
// account is ../at/README.md, at(1) being where both programs are documented.
//
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ATDIR "/usr/spool/at"
#define PDIR  "past"
#define LASTF "/usr/spool/at/lasttimedone"

static int nowtime;
static int nowdate;
static int nowyear;

static void makenowtime(void);
static void updatetime(int t);
static void run(char *file);

int main(void)
{
    int tt, day, year, uniq;
    DIR *dirp;
    struct dirent *dp;

    if (chdir(ATDIR) < 0) {
        fprintf(stderr, "atrun: cannot chdir to %s\n", ATDIR);
        exit(1);
    }
    makenowtime();
    if ((dirp = opendir(".")) == NULL) {
        fprintf(stderr, "Cannot read at directory\n");
        exit(1);
    }
    while ((dp = readdir(dirp)) != NULL) {
        if (sscanf(dp->d_name, "%4d.%3d.%4d.%2d", &year, &day, &tt, &uniq) != 4)
            continue;
        if (nowyear < year)
            continue;
        if (nowyear == year && nowdate < day)
            continue;
        if (nowyear == year && nowdate == day && nowtime < tt)
            continue;
        run(dp->d_name);
    }
    closedir(dirp);
    updatetime(nowtime);
    exit(0);
}

static void makenowtime(void)
{
    time_t t;
    struct tm *tp;

    time(&t);
    tp      = localtime(&t);
    nowtime = tp->tm_hour * 100 + tp->tm_min;
    nowdate = tp->tm_yday;
    nowyear = tp->tm_year + 1900;
}

static void updatetime(int t)
{
    FILE *tfile;

    tfile = fopen(LASTF, "w");
    if (tfile == NULL) {
        fprintf(stderr, "can't write lastfile\n");
        exit(1);
    }
    fprintf(tfile, "%04d\n", t);
    fclose(tfile);
}

static void run(char *file)
{
    struct stat stbuf;
    char past[DIRSIZ + 8]; // "past/" + a name of at most DIRSIZ + the NUL
    int pid, i;

    // The owner of the copy is who the job runs as, so it is read before the move -- and a
    // job with a second link is refused: the spool is mode 0777, so a hard link to somebody
    // else's file would otherwise choose the uid this exec runs under.  Both are done in the
    // parent, where a diagnostic still has somewhere to go.
    if (stat(file, &stbuf) == -1)
        return;
    if (stbuf.st_nlink != 1) {
        fprintf(stderr, "atrun: %s has %d links -- not run\n", file, (int)stbuf.st_nlink);
        return;
    }
    if (fork() != 0)
        return;
    for (i = 0; i < NOFILE; i++)
        close(i);
    dup(dup(open("/dev/null", 0)));
    // v7 forked /bin/sh to fork /bin/mv.  Same directory tree and we are root, so this is
    // the whole of what mv would do.
    sprintf(past, "%s/%s", PDIR, file);
    if (link(file, past) < 0 || unlink(file) < 0)
        _exit(1);
    if (chdir(PDIR) < 0)
        _exit(1);
    setgid(stbuf.st_gid);
    setuid(stbuf.st_uid);
    if ((pid = fork())) {
        if (pid == -1)
            _exit(1);
        wait((int *)0);
        unlink(file);
        _exit(0);
    }
    nice(3);
    execl("/bin/sh", "sh", file, (char *)0);
    fprintf(stderr, "Can't execl shell\n"); // to /dev/null, as in v7: the status is the signal
    _exit(1);
}
