/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// pwd -- print the working directory.
//
// The v7 program, unchanged in what it does.  It is the first command here that reads the
// FILESYSTEM'S OWN FORMAT rather than a stream of bytes, which is what makes it interesting:
// there is no getcwd() in v7 and no way to ask the kernel where you are, so pwd walks UP,
// one `..' at a time, reading each parent directory to find the entry whose inode number
// matches the child it just came from, and builds the path backwards.
//
// The C11 pass is the usual one -- prototypes, explicit return types, `static' on everything
// file-local, and `register i, j;' (untyped, so implicitly int) spelled out.
//
// THE PARENT IS READ WITH opendir(3), task C24.  d_ino is what pwd matches on and struct
// dirent carries it, so the algorithm is v7's; what goes is the raw read(), and with it the
// two fixes this port had to make to it -- a signed/unsigned comparison and an unterminated
// name.  README.md has both.  dirfd(3) is how the mount-point test still fstat()s the parent.
//
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// How much path pwd can build.  v7 wrote 512 and then 511 as a bare number in cat(); both
// are this one constant now.
#define NAMEBUF 512

static char dot[]    = ".";
static char dotdot[] = "..";
static char name[NAMEBUF]; // the path so far, built up from the RIGHT
static int off = -1;       // index of the last character in name[]; -1 while it is empty
static struct stat d, dd;

static void prname(void);           // print name[] and exit -- pwd's only exit
static void cat(const char *, int); // prepend one component to name[]

int main(void)
{
    DIR *dirp;
    struct dirent *dp;
    int rdev, rino;

    // The root is where the walk stops, and it is recognised by its device and inode number
    // rather than by its name -- the name `/' is the one thing a directory entry never holds.
    stat("/", &d);
    rdev = d.st_dev;
    rino = d.st_ino;

    for (;;) {
        // Where are we now?
        stat(dot, &d);
        if (d.st_ino == rino && d.st_dev == rdev)
            prname(); // arrived at the root: print the path and exit

        if ((dirp = opendir(dotdot)) == NULL) {
            fprintf(stderr, "pwd: cannot open ..\n");
            exit(1);
        }
        fstat(dirfd(dirp), &dd);
        chdir(dotdot); // step up; from here `.' is the parent and `dirp' is its contents

        if (d.st_dev == dd.st_dev) {
            // The ordinary case: parent and child on one filesystem, so the child's inode
            // number is meaningful in the parent's directory and can simply be looked up.
            if (d.st_ino == dd.st_ino)
                prname(); // `..' is itself -- the root of a filesystem that is not `/'
            while ((dp = readdir(dirp)) != NULL && dp->d_ino != d.st_ino)
                ;
        } else {
            // A MOUNT POINT.  The child is the root of a mounted filesystem and its inode
            // number means nothing in the parent, so every entry has to be stat()ed until one
            // of them turns out to BE the child -- same device and same inode.
            while ((dp = readdir(dirp)) != NULL) {
                stat(dp->d_name, &dd);
                if (dd.st_ino == d.st_ino && dd.st_dev == d.st_dev)
                    break;
            }
        }
        if (dp == NULL) {
            fprintf(stderr, "read error in ..\n");
            exit(1);
        }

        // Before the closedir(): dp points into the stream's own storage.
        cat(dp->d_name, dp->d_namlen);
        closedir(dirp);
    }
}

//
// Print what has been built and exit.  Every path starts with the `/' written here, and
// name[] carries the rest -- so the root itself, where off is still -1, prints as `/'.
//
static void prname(void)
{
    write(1, "/", 1);
    if (off < 0)
        off = 0;
    name[off] = '\n';
    write(1, name, off + 1);
    exit(0);
}

//
// Prepend the component just found to the path: name[] becomes "<comp>/<name>".  It is
// built this way round because the walk discovers the components in reverse -- the deepest
// first -- and shifting the buffer right is cheaper than reversing it at the end.
//
static void cat(const char *comp, int i)
{
    int j;

    // No room for another component: print what there is rather than overrun the buffer.
    if (off + i + 2 > NAMEBUF - 1)
        prname();

    // Shift the path right by i+1 characters to make room for the name and its slash ...
    for (j = off + 1; j >= 0; --j)
        name[j + i + 1] = name[j];
    off = i + off + 1;

    // ... and write them into the hole, the slash first since the loop runs backwards.
    name[i] = '/';
    for (--i; i >= 0; --i)
        name[i] = comp[i];
}
