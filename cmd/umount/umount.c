/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// umount -- dismount a file system.
//
//	/etc/umount special
//
// Task C4f, the other half of ../mount/mount.c, whose header is the account of both.  The
// table this maintains is ../mount/mtab.c, compiled into this program a second time rather
// than copied into it: v7 gives each program its own `struct mtab', its own read loop and
// its own write loop, and this one's copy is the one that clears the entry with a `char *'
// comparison against &mp->file[NAMSIZ*2].
//
// WHAT sumount() REFUSES, since these are the diagnostics a user meets:
//
//   * ENOTBLK -- `special' must be the BLOCK device, /dev/md1 and not /dev/rmd1.
//   * EINVAL  -- nothing is mounted on it.  This is also what a second umount answers.
//   * EBUSY   -- somebody is still on the filesystem.  An open file or a process whose
//                working directory is inside it both count, and the shell that typed the
//                command is a process: `cd /mnt; /etc/umount /dev/md1' is EBUSY every time.
//
// THE ORDER IS v7's AND IS DELIBERATE: sync(2), then umount(2), then the table.  The sync is
// what puts the mounted filesystem's delayed writes on the disk while it is still mounted --
// sumount() calls update() itself, so this is belt and braces, and it is also what makes the
// command safe to type before pulling a pack.
//
// ONE DIVERGENCE from v7, in the source and in mount.1m (../README.md SS10): a special file
// that umount(2) accepted but that /etc/mtab does not name is a WARNING and exit 0, where v7
// prints "not in mount table" and exits 1.  The filesystem really is unmounted by then, and
// mtab(5) has said since v7 that it does not matter to umount if a name cannot be found; an
// exit status that says the unmount failed when it succeeded is worse than a stale table.
//

#include <stdio.h>
#include <unistd.h>

#include "mtab.h"

int main(int argc, char **argv)
{
    char spec[NAMSIZ];
    int i;

    if (argc != 2) {
        fprintf(stderr, "usage: umount special\n");
        return 1;
    }
    if (!mtabname(spec, argv[1], "umount"))
        return 1;

    sync();
    if (umount(spec) < 0) {
        perror("umount");
        return 1;
    }

    mtabread();
    i = mtabfind(spec);
    if (i < 0) {
        fprintf(stderr, "umount: %s unmounted, but %s does not name it\n", spec, MTAB);
        return 0;
    }
    for (; i < nmtab - 1; i++)
        mtab[i] = mtab[i + 1];
    nmtab--;

    mtabwrite();
    return 0;
}
