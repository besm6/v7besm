// statfs(2): how much room a mounted filesystem has left.
//
// df(1) used to read the superblock off /dev/rmd0, mode 0600 because that node is every
// file's contents, and so was the super-user's program.  These four counts are not secret;
// handing them over without handing over the device is the whole of why this call exists.
// They also come from the IN-CORE superblock, which is the current one -- the disk's copy is
// stale until update() writes it back.  cmd/df/README.md is the account.
//
// It is a system call and not an operation on kctl(2) because kctl's first argument is a
// symbol name, read for at most KSYMLEN (12) bytes: a path would not fit.
#ifndef _SYS_STATFS_H
#define _SYS_STATFS_H

#include <sys/types.h> // daddr_t, ino_t

// No geometry (s_bsize, s_inopb, s_naddr), no magic, no device number and no mount point:
// the numbers came out of a superblock this kernel mounted and sbcheck()ed, so they cannot
// disagree with the <sys/param.h> both sides compile against, and nothing reads the rest.
struct statfs {
    daddr_t f_fsize; // s_fsize:  blocks in the entire volume
    int f_isize;     // s_isize:  blocks of i-list, the superblock's own block included
    daddr_t f_tfree; // s_tfree:  free blocks
    ino_t f_tinode;  // s_tinode: free i-nodes
};

// int statfs(const char *path, struct statfs *buf)
//
// Reports on the filesystem holding `path'.  namei() crosses a mount, so statfs("/mnt")
// answers for what is mounted there, not for the directory it covers.  Returns 0, or -1 and
//
//      ENOENT, ENOTDIR, EACCES  from the path walk, as stat(2)
//      ENXIO                    the filesystem is not mounted
//      EFAULT                   path or buf unreadable
//
// NOT PRIVILEGED: four counts about a volume the caller can already name a file on.
//
// Guarded like <sys/kctl.h>'s block: the kernel's handler is `void statfs(void)'.
#ifndef KERNEL
int statfs(const char *path, struct statfs *buf);
#endif

#endif // _SYS_STATFS_H
