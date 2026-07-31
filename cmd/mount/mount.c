/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// mount -- mount a file system.
//
//	/etc/mount [ special directory [ -r ] ]
//
// Task C4f, and the point of it is that there is at last a second thing to mount: C4c gave
// the machine a second drive and a program to make a filesystem on it, and until this one
// there was no way to reach that filesystem except as a raw device.  A mounted volume is
// read and written through the BUFFER CACHE -- bread(), bwrite(), bdwrite() in
// kernel/dev/bio.c -- where everything C4 has written so far went through physio().
// bdevsw[0] minor 1 had never carried a block.
//
// THE KERNEL SIDE IS OLDER THAN THE PROGRAM.  smount() (kernel/sys3.c) has been compiled
// into every unix this port has ever built and had no caller at all: iinit() fills mount[0]
// with the root by hand rather than going through it.  So this file is the first thing that
// has ever exercised it, and three of its answers are worth knowing before reading the
// diagnostics below:
//
//   * `special' must be a BLOCK device -- getmdev() insists on IFBLK -- so it is /dev/md1
//     and never /dev/rmd1, which is the same drive through cdevsw[3] and answers ENOTBLK.
//     That is the one argument mistake this command invites, every other C4 program taking
//     the raw name.
//
//   * NMOUNT is 2 and the root holds one slot, so exactly ONE filesystem may be mounted.
//     The second attempt is EBUSY, which is also what a device already mounted and a mount
//     point already in use both answer.
//
//   * A DEVICE WITH NO FILESYSTEM ON IT IS REFUSED, which retires this program's oldest
//     BUG.  v7's mount(2) reads the superblock and believes it, so mount.1m says in so many
//     words that "mounting file systems full of garbage will crash the system"; this kernel
//     calls sbcheck() (kernel/alloc.c) and answers EINVAL after printing what it disliked on
//     the console.  mount.1m says so now instead.
//
// TWO DIVERGENCES FROM v7, both small and both in the source and the manual page (../README.md
// SS10):
//
//   * `-r' IS PARSED.  v7 mounts read-only if argc > 3 and never looks at the flag, so
//     `mount /dev/md1 /mnt rw' quietly mounts read-only.  Here anything but -r is refused.
//
//   * EVERY ARGUMENT IS SETTLED BEFORE /etc/mtab IS OPENED.  v7 reads the table first and
//     diagnoses afterwards.  The order matters to the test rather than to the user: it is
//     what makes the argument diagnostics assertable under b6sim, where the program would
//     otherwise read the BUILD MACHINE's /etc/mtab (../df/README.md's getpwent(3) hazard,
//     in another guise).  Nothing else here can be tested outside SIMH -- see README.md.
//
// NOT SETUID.  /dev/md1 is mode 0600 and root's, which is the whole of the privilege story;
// mount(2) itself asks for none (v7 gates it on the mode of the special file, and this
// kernel kept that).  ../README.md SS8.
//

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "mtab.h"

int main(int argc, char **argv)
{
    struct mtab ent;
    int i;

    // Before the table is opened.  See the head comment: it is what makes this diagnostic,
    // the flag one below and mtabname()'s assertable under b6sim, where mtabread() would
    // read the build machine's /etc/mtab and print whatever it disliked about it.
    if (argc == 2 || argc > 4) {
        fprintf(stderr, "usage: mount [ special directory [ -r ] ]\n");
        return 1;
    }
    ent.m_ro = 0;
    if (argc == 4) {
        if (strcmp(argv[3], "-r") != 0) {
            fprintf(stderr, "mount: unknown flag %s\n", argv[3]);
            return 1;
        }
        ent.m_ro = 1;
    }

    if (argc == 1) {
        mtabread();
        for (i = 0; i < nmtab; i++)
            printf("%s on %s%s\n", mtab[i].m_spec, mtab[i].m_dir,
                   mtab[i].m_ro ? " (read-only)" : "");
        return 0;
    }

    // The names are canonicalized and bounded before the mount, so that a name this table
    // cannot hold is refused while refusing it still costs nothing.  The other order leaves
    // a filesystem mounted that nothing has recorded.
    if (!mtabname(ent.m_spec, argv[1], "mount") || !mtabname(ent.m_dir, argv[2], "mount"))
        return 1;

    mtabread();
    if (nmtab >= NMTAB) {
        fprintf(stderr, "mount: %s is full\n", MTAB);
        return 1;
    }

    if (mount(ent.m_spec, ent.m_dir, ent.m_ro) < 0) {
        perror("mount");
        return 1;
    }
    mtab[nmtab++] = ent;

    // The mount has happened; a table that cannot be written is worth a diagnostic and not
    // a failing exit status.  mtabwrite() has printed it.
    mtabwrite();
    return 0;
}
