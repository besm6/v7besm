// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// ttyname(f) -- "/dev/ttyXX", the name of the terminal open on descriptor f, or NULL.
//
// v7's method, which is a linear search of /dev: there is no way back from a device
// number to a name, so the directory is read and every entry with the right i-number is
// stat'ed to make sure it really is the same file.  IT IS AN ORDINARY read() HERE, as it
// was in v7, so this works on the kernel and NOT under b6sim, whose read() is the host's
// and refuses a directory.  Under the simulator it simply answers NULL.
//
// THERE IS AN opendir(3) NOW -- see directory(3) -- AND THIS DELIBERATELY DOES NOT USE IT.
// The library costs about 230 words of text plus a heap allocation per open directory, and
// /bin/login and /etc/getty would pay both for a routine that scans /dev once and is never
// called again.  The four lines below cost nothing and are already correct.  cmd/README.md
// SS6's rule applies to a library exactly as it does to a program: ask what is still going to
// be needed after the convenience is bought.
//
// Two fixes to v7, both about the name field.  struct direct is DIRSIZ chars with no
// room for a terminator when the name fills it (include/sys/dir.h), so the copy is
// bounded: v7's strcat() would have run past the entry into the next one.  And rbuf is
// sized from DIRSIZ rather than being v7's flat 32, since DIRSIZ is 18 here and not 14.
//
// <unistd.h> declares it, beside ttyslot(), which answers its question the other way.
//
#include <fcntl.h>
#include <string.h>
#include <sys/dir.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static const char dev[] = "/dev/";

//
// "/dev/" plus a full-width entry name plus the terminator.  Written out because
// b6cc will not take a `sizeof' in an array bound -- "Array size is not literal" --
// so the static assertion below is what keeps the two in step.
//
#define DEVLEN 5
#define RBUFSZ (DEVLEN + DIRSIZ + 1)

_Static_assert(sizeof dev == DEVLEN + 1, "DEVLEN must be the length of dev[]");

char *ttyname(int f)
{
    struct stat fsb;
    struct stat tsb;
    struct direct db;
    static char rbuf[RBUFSZ];
    int df;

    if (isatty(f) == 0)
        return NULL;
    if (fstat(f, &fsb) < 0)
        return NULL;
    if ((fsb.st_mode & S_IFMT) != S_IFCHR)
        return NULL;
    if ((df = open(dev, O_RDONLY)) < 0)
        return NULL;

    while (read(df, (char *)&db, sizeof db) == sizeof db) {
        if (db.d_ino == 0)
            continue;
        if (db.d_ino != fsb.st_ino)
            continue;
        strcpy(rbuf, dev);
        strncat(rbuf, db.d_name, DIRSIZ);
        if (stat(rbuf, &tsb) < 0)
            continue;
        if (tsb.st_dev == fsb.st_dev && tsb.st_ino == fsb.st_ino) {
            close(df);
            return rbuf;
        }
    }
    close(df);
    return NULL;
}
