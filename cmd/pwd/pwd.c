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
// file-local, and `register i, j;' (untyped, so implicitly int) spelled out.  `open(dotdot, 0)'
// is O_RDONLY now that <fcntl.h> names it, as cmd/init did.
//
// TWO REAL FIXES, both of which the BESM-6 turns from latent into live:
//
//  1. `read(file, &dir, sizeof(dir)) < sizeof(dir)' is a SIGNED/UNSIGNED comparison.  read()
//     returns an int; sizeof yields an unsigned type (sys/param.h says so, and all of libc
//     writes `(int)sizeof'), so the usual arithmetic conversions promote the -1 of a failed
//     read to 2^48-1 and the test is FALSE -- a failed read looks like a good one, `dir'
//     holds whatever was there before, and the do/while below either spins on stale data or
//     matches by accident and prints a wrong path.  The end-of-directory 0 compares correctly
//     either way, which is exactly why the common path works and hides it.  Cast, at both
//     sites.
//
//  2. `while (dir.d_name[++i] != 0);' assumes the name is terminated.  d_name is a bare
//     char[DIRSIZ] and the kernel zero-PADS a short name; a name of exactly DIRSIZ characters
//     fills the array with no NUL, and the scan walks off the end of the struct.  That was a
//     v7 bug at 14 characters and is the same bug at 18.  Both scans are bounded now, and the
//     mount-crossing branch -- which hands d_name straight to stat(), which would read past
//     the struct for the same reason -- copies into a terminated buffer first.
//
// DIRSIZ IS 18 HERE, not v7's 14, and a struct direct is four words with a full-word d_ino
// (sys/param.h, sys/dir.h).  Nothing in this file hardcoded either number, so the only
// consequence is that a 512-character buffer now holds about 27 path components instead of 34.
//
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/dir.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// How much path pwd can build.  v7 wrote 512 and then 511 as a bare number in cat(); both
// are this one constant now.
#define NAMEBUF 512

static char dot[]    = ".";
static char dotdot[] = "..";
static char name[NAMEBUF]; // the path so far, built up from the RIGHT
static int file;           // the open `..' that cat() is reading
static int off = -1;       // index of the last character in name[]; -1 while it is empty
static struct stat d, dd;
static struct direct dir;

static void prname(void); // print name[] and exit -- pwd's only exit
static void cat(void);    // prepend dir.d_name to name[]

int main(void)
{
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

        if ((file = open(dotdot, O_RDONLY)) < 0) {
            fprintf(stderr, "pwd: cannot open ..\n");
            exit(1);
        }
        fstat(file, &dd);
        chdir(dotdot); // step up; from here `.' is the parent and `file' is its contents

        if (d.st_dev == dd.st_dev) {
            // The ordinary case: parent and child on one filesystem, so the child's inode
            // number is meaningful in the parent's directory and can simply be looked up.
            if (d.st_ino == dd.st_ino)
                prname(); // `..' is itself -- the root of a filesystem that is not `/'
            do {
                if (read(file, (char *)&dir, sizeof(dir)) < (int)sizeof(dir)) {
                    fprintf(stderr, "read error in ..\n");
                    exit(1);
                }
            } while (dir.d_ino != d.st_ino);
        } else {
            // A MOUNT POINT.  The child is the root of a mounted filesystem and its inode
            // number means nothing in the parent, so every entry has to be stat()ed until one
            // of them turns out to BE the child -- same device and same inode.
            char nbuf[DIRSIZ + 1];
            int i;

            do {
                if (read(file, (char *)&dir, sizeof(dir)) < (int)sizeof(dir)) {
                    fprintf(stderr, "read error in ..\n");
                    exit(1);
                }
                // d_name may fill the array with no room for a NUL; stat() would read past
                // the struct.
                for (i = 0; i < DIRSIZ; i++)
                    nbuf[i] = dir.d_name[i];
                nbuf[DIRSIZ] = '\0';
                stat(nbuf, &dd);
            } while (dd.st_ino != d.st_ino || dd.st_dev != d.st_dev);
        }

        close(file);
        cat();
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
// Prepend the component just found to the path: name[] becomes "<d_name>/<name>".  It is
// built this way round because the walk discovers the components in reverse -- the deepest
// first -- and shifting the buffer right is cheaper than reversing it at the end.
//
static void cat(void)
{
    int i, j;

    // The component's length, bounded: see the header.
    for (i = 0; i < DIRSIZ && dir.d_name[i] != 0; i++)
        ;

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
        name[i] = dir.d_name[i];
}
