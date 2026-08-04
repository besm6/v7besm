//
// dirt -- opendir(3), readdir(3) and the rest of directory(3).
//
// IMAGEONLY, and the reason is sharper than the other five.  b6sim maps every system call
// onto the host, and on the host open("/usr/test", O_RDONLY) SUCCEEDS and fstat() reports
// S_IFDIR -- only read(2) refuses, with EISDIR.  So under the simulator opendir() returns
// a perfectly good DIR and the first readdir() returns NULL: every directory in the world
// looks empty, and looks it in exactly the way an empty directory does.  One .expected
// cannot adjudicate that and the real thing, and a library that answers "no entries" is
// worse than one that fails, so this program runs on the disk image only (progs.cmake).
//
// WHAT IT ASSERTS, in the order the library has to get right:
//
//   - opendir() refuses a plain file with ENOTDIR.  Nothing else can catch this: read(2)
//     on a regular file succeeds here, so a missing test means readdir() hands back file
//     bytes reinterpreted as directory entries, and there is no field in a struct direct
//     that can be checked for sense afterwards.
//   - a name of every interesting length comes back whole and NUL-terminated -- one
//     character, DIRSIZ-1, and EXACTLY DIRSIZ, which is the case with no terminator on the
//     disk at all and the reason struct dirent is DIRSIZ + 1.
//   - d_namlen agrees with strlen(), so a caller can trust it and skip the scan.
//   - readdir() skips the holes.  unlink(2) empties an entry by writing d_ino = 0 back in
//     place (kernel/sys4.c), so unlinking the MIDDLE of three leaves a real hole with live
//     entries after it -- which is the only arrangement that tests the skip.
//   - telldir()/seekdir() name a position that survives reading past it, and rewinddir()
//     replays the same sequence.
//   - dirfd() names the directory itself, which is how ls(1) tells a directory from the
//     plain file it was handed.
//
// /usr/test/scratch is its own, empty, and in root.manifest for that reason: it is the one
// way to assert a COMPLETE listing.  Everything this program creates it removes again, so
// the filesystem it leaves is the one it found.
//
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define SCRATCH "/usr/test/scratch"

// One character, DIRSIZ - 2 and DIRSIZ - 1 -- and `full' below is exactly DIRSIZ, which is
// the whole point: on the disk that one fills d_name with no room for a terminator, so
// anything reading it with strlen() runs into the next entry's d_ino.
static const char *const names[3] = {
    "a",
    "bcdefghijklmnopq",  // 16 == DIRSIZ - 2
    "cdefghijklmnopqrs", // 17 == DIRSIZ - 1
};

static char full[DIRSIZ + 1]; // built at run time: exactly DIRSIZ characters

static void ok(const char *what, int cond)
{
    printf("%s %s\n", cond ? "ok  " : "FAIL", what);
}

//
// Walk a directory into buf as "name name name", and return how many entries there were.
// Also checks the two things every entry must satisfy, whatever the directory is.
//
static int walk(DIR *dirp, char *buf, int bufsz, int *clean)
{
    struct dirent *dp;
    int n = 0, len = 0;

    buf[0] = '\0';
    while ((dp = readdir(dirp)) != NULL) {
        if (dp->d_ino == 0)
            *clean = 0; // a hole readdir() should have skipped
        if (dp->d_namlen != (int)strlen(dp->d_name))
            *clean = 0; // d_namlen must agree with the terminator it planted
        if (len + dp->d_namlen + 2 < bufsz) {
            if (n > 0)
                buf[len++] = ' ';
            strcpy(buf + len, dp->d_name);
            len += dp->d_namlen;
        }
        n++;
    }
    return n;
}

static void make(const char *dir, const char *name)
{
    char path[64];
    int fd;

    strcpy(path, dir);
    strcat(path, "/");
    strcat(path, name);
    if ((fd = creat(path, 0644)) >= 0)
        close(fd);
}

static void unmake(const char *dir, const char *name)
{
    char path[64];

    strcpy(path, dir);
    strcat(path, "/");
    strcat(path, name);
    unlink(path);
}

int main(void)
{
    DIR *dirp;
    struct dirent *dp;
    struct stat sb, sb2;
    char buf[256], buf2[256], mark[DIRSIZ + 1];
    long loc;
    int i, n, clean;

    for (i = 0; i < DIRSIZ; i++)
        full[i] = 'A' + i;
    full[DIRSIZ] = '\0';

    printf("--- opendir\n");
    dirp = opendir("/");
    ok("the root directory opens", dirp != NULL);

    printf("--- the root's first two entries\n");
    clean = 1;
    dp    = readdir(dirp);
    ok("the first entry is .", dp != NULL && strcmp(dp->d_name, ".") == 0);
    ok("and it is the root inode", dp != NULL && dp->d_ino == ROOTINO);
    dp = readdir(dirp);
    ok("the second entry is ..", dp != NULL && strcmp(dp->d_name, "..") == 0);
    ok("and the root's parent is itself", dp != NULL && dp->d_ino == ROOTINO);

    printf("--- dirfd\n");
    ok("dirfd names an open descriptor", fstat(dirfd(dirp), &sb) == 0);
    ok("and it is a directory", (sb.st_mode & S_IFMT) == S_IFDIR);
    ok("and it is the one that was opened",
       stat("/", &sb2) == 0 && sb.st_ino == sb2.st_ino && sb.st_dev == sb2.st_dev);
    closedir(dirp);

    printf("--- opendir refuses what is not a directory\n");
    errno = 0;
    dirp = opendir("/etc/passwd");
    i    = errno; // taken before the printf in ok(), which may set errno of its own
    ok("a plain file is refused", dirp == NULL);
    ok("and the reason is ENOTDIR", i == ENOTDIR);
    ok("a path that does not exist is refused", opendir("/no/such/thing") == NULL);

    printf("--- names of every length\n");
    make(SCRATCH, names[0]);
    make(SCRATCH, names[1]);
    make(SCRATCH, names[2]);
    make(SCRATCH, full);

    dirp = opendir(SCRATCH);
    ok("the scratch directory opens", dirp != NULL);
    clean = 1;
    n     = walk(dirp, buf, sizeof buf, &clean);
    printf("%d entries: %s\n", n, buf);
    ok("no entry has a zero i-number", clean);
    ok("six entries: . .. and the four made here", n == 6);

    printf("--- a name that fills the field\n");
    rewinddir(dirp);
    n = 0;
    while ((dp = readdir(dirp)) != NULL)
        if (strcmp(dp->d_name, full) == 0)
            n++;
    ok("the DIRSIZ-character name came back whole", n == 1);

    printf("--- a hole in the middle\n");
    unmake(SCRATCH, names[1]);
    rewinddir(dirp);
    clean = 1;
    n     = walk(dirp, buf, sizeof buf, &clean);
    printf("%d entries: %s\n", n, buf);
    ok("the emptied slot was skipped", clean);
    ok("one fewer entry", n == 5);

    printf("--- telldir and seekdir\n");
    rewinddir(dirp);
    readdir(dirp); // .
    readdir(dirp); // ..
    loc = telldir(dirp);
    dp  = readdir(dirp);
    strcpy(mark, dp == NULL ? "(null)" : dp->d_name);
    readdir(dirp);
    readdir(dirp);
    seekdir(dirp, loc);
    dp = readdir(dirp);
    ok("seeking back to a marked entry finds it again",
       dp != NULL && strcmp(dp->d_name, mark) == 0);

    printf("--- rewinddir\n");
    rewinddir(dirp);
    clean = 1;
    n     = walk(dirp, buf2, sizeof buf2, &clean);
    rewinddir(dirp);
    clean = 1;
    i     = walk(dirp, buf, sizeof buf, &clean);
    ok("two walks agree", n == i && strcmp(buf2, buf) == 0);
    closedir(dirp);

    unmake(SCRATCH, names[0]);
    unmake(SCRATCH, names[2]);
    unmake(SCRATCH, full);

    printf("--- and it is empty again\n");
    dirp  = opendir(SCRATCH);
    clean = 1;
    n     = walk(dirp, buf, sizeof buf, &clean);
    printf("%d entries: %s\n", n, buf);
    ok("only . and ..", n == 2);
    closedir(dirp);

    printf("done\n");
    return 0;
}
