// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

#include "sys/buf.h"
#include "sys/conf.h"
#include "sys/dir.h"
#include "sys/file.h"
#include "sys/filsys.h"
#include "sys/ino.h"
#include "sys/inode.h"
#include "sys/mount.h"
#include "sys/param.h"
#include "sys/reg.h"
#include "sys/stat.h"
#include "sys/statfs.h"
#include "sys/systm.h"
#include "sys/types.h"
#include "sys/user.h"

void stat1(register struct inode *ip, struct stat *ub, off_t pipeadj);

// the fstat system call.
void fstat()
{
    register struct file *fp;
    register struct a {
        int fdes;
        struct stat *sb;
    } *uap;

    uap = (struct a *)u.u_ap;
    fp  = getf(uap->fdes);
    if (fp == NULL)
        return;
    stat1(fp->f_inode, uap->sb, fp->f_flag & FPIPE ? fp->f_un.f_offset : 0);
}

// the stat system call.
void stat()
{
    register struct inode *ip;
    register struct a {
        char *fname;
        struct stat *sb;
    } *uap;

    uap = (struct a *)u.u_ap;
    ip  = namei(uchar, 0);
    if (ip == NULL)
        return;
    stat1(ip, uap->sb, (off_t)0);
    iput(ip);
}

// The basic routine for fstat and stat:
// get the inode and pass appropriate parts back.
void stat1(register struct inode *ip, struct stat *ub, off_t pipeadj)
{
    register struct dinode *dp;
    register struct buf *bp;
    struct stat ds;

    iupdat(ip, &time, &time);
    // first copy from inode table
    ds.st_dev   = ip->i_dev;
    ds.st_ino   = ip->i_number;
    ds.st_mode  = ip->i_mode;
    ds.st_nlink = ip->i_nlink;
    ds.st_uid   = ip->i_uid;
    ds.st_gid   = ip->i_gid;
    ds.st_rdev  = (dev_t)ip->i_un.i_rdev;
    ds.st_size  = ip->i_size - pipeadj;
    // next the dates in the disk
    bp = bread(ip->i_dev, itod(ip->i_number));
    dp = (struct dinode *)bp->b_addr;
    dp += itoo(ip->i_number);
    ds.st_atime = dp->di_atime;
    ds.st_mtime = dp->di_mtime;
    ds.st_ctime = dp->di_ctime;
    brelse(bp);
    if (copyout((caddr_t)&ds, (caddr_t)ub, sizeof(ds)) < 0)
        u.u_error = EFAULT;
}

// the dup system call.
void dup()
{
    register struct file *fp;
    register struct a {
        int fdes;
        int fdes2;
    } *uap;
    register int i, m;

    uap = (struct a *)u.u_ap;
    m   = uap->fdes & ~077;
    uap->fdes &= 077;
    fp = getf(uap->fdes);
    if (fp == NULL)
        return;
    if ((m & 0100) == 0) {
        if ((i = ufalloc()) < 0)
            return;
    } else {
        i = uap->fdes2;
        if (i < 0 || i >= NOFILE) {
            u.u_error = EBADF;
            return;
        }
        u.u_r.r_val1 = i;
    }
    if (i != uap->fdes) {
        if (u.u_ofile[i] != NULL)
            closef(u.u_ofile[i]);
        u.u_ofile[i] = fp;
        // dup2 names fdes2 outright, bypassing ufalloc(): clear the stale EXCLOSE.
        u.u_pofile[i] = 0;
        fp->f_count++;
    }
}

// the mount system call.
void smount()
{
    dev_t dev;
    register struct inode *ip;
    register struct mount *mp;
    struct mount *smp;
    register struct filsys *fp;
    struct buf *bp;
    register struct a {
        char *fspec;
        char *freg;
        int ronly;
    } *uap;

    uap = (struct a *)u.u_ap;
    dev = getmdev();
    if (u.u_error)
        return;
    u.u_dirp = (caddr_t)uap->freg;
    ip       = namei(uchar, 0);
    if (ip == NULL)
        return;
    if (ip->i_count != 1 || (ip->i_mode & (IFBLK & IFCHR)) != 0)
        goto out;
    smp = NULL;
    for (mp = &mount[0]; mp < &mount[NMOUNT]; mp++) {
        if (mp->m_bufp != NULL) {
            if (dev == mp->m_dev)
                goto out;
        } else if (smp == NULL)
            smp = mp;
    }
    mp = smp;
    if (mp == NULL)
        goto out;
    (*bdevsw[major(dev)].d_open)(dev, !uap->ronly);
    if (u.u_error)
        goto out;
    bp = bread(dev, SUPERB);
    if (u.u_error) {
        brelse(bp);
        goto out1;
    }
    // Refuse it here rather than let getfs() "repair" it into a full disk later.
    if (sbcheck((struct filsys *)bp->b_addr, dev)) {
        brelse(bp);
        u.u_error = EINVAL;
        goto out1;
    }
    mp->m_inodp = ip;
    mp->m_dev   = dev;
    mp->m_bufp  = geteblk();
    wcopy((caddr_t)bp->b_addr, mp->m_bufp->b_addr, BSIZEW);
    fp          = (struct filsys *)mp->m_bufp->b_addr;
    fp->s_ilock = 0;
    fp->s_flock = 0;
    fp->s_ronly = uap->ronly & 1;
    brelse(bp);
    ip->i_flag |= IMOUNT;
    prele(ip);
    return;

out:
    u.u_error = EBUSY;
out1:
    iput(ip);
}

// the umount system call.
void sumount()
{
    dev_t dev;
    register struct inode *ip;
    register struct mount *mp;
    struct buf *bp;
    struct a {
        char *fspec;
    };

    dev = getmdev();
    if (u.u_error)
        return;
    xumount(dev); // remove unused sticky files from text table
    update();
    for (mp = &mount[0]; mp < &mount[NMOUNT]; mp++)
        if (mp->m_bufp != NULL && dev == mp->m_dev)
            goto found;
    u.u_error = EINVAL;
    return;

found:
    for (ip = &inode[0]; ip < &inode[NINODE]; ip++)
        if (ip->i_number != 0 && dev == ip->i_dev) {
            u.u_error = EBUSY;
            return;
        }
    (*bdevsw[major(dev)].d_close)(dev, 0);
    ip = mp->m_inodp;
    ip->i_flag &= ~IMOUNT;
    plock(ip);
    iput(ip);
    bp         = mp->m_bufp;
    mp->m_bufp = NULL;
    brelse(bp);
}

// int statfs(const char *path, struct statfs *buf) -- <sys/statfs.h>.
//
// It walks mount[] itself rather than calling getfs(), which panics on an unmounted device
// and, on a bad in-core count, zeroes s_nfree/s_ninode and prints to the console.  A read-only
// query must do neither.  The ENXIO arm is unreachable through namei() today, and written
// anyway: that is the same "cannot happen" getfs() rests on.
void statfs()
{
    register struct inode *ip;
    register struct mount *mp;
    register struct filsys *fp;
    register struct a {
        char *fname;
        struct statfs *buf;
    } *uap = (struct a *)u.u_ap;
    struct statfs sf;

    // u.u_dirp is already uap->fname (kernel/syscall.c).
    ip = namei(uchar, 0);
    if (ip == NULL)
        return;

    for (mp = &mount[0]; mp < &mount[NMOUNT]; mp++)
        if (mp->m_bufp != NULL && mp->m_dev == ip->i_dev)
            goto found;
    u.u_error = ENXIO;
    iput(ip);
    return;

found:
    fp          = (struct filsys *)mp->m_bufp->b_addr;
    sf.f_fsize  = fp->s_fsize;
    sf.f_isize  = fp->s_isize;
    sf.f_tfree  = fp->s_tfree;
    sf.f_tinode = fp->s_tinode;
    iput(ip);

    // copyout() and not copyoutb(): word-sized fields into a struct, as stat1() above.
    if (copyout((caddr_t)&sf, (caddr_t)uap->buf, sizeof sf) < 0)
        u.u_error = EFAULT;
}

// Common code for mount and umount.
// Check that the user's argument is a reasonable
// thing on which to mount, and return the device number if so.
dev_t getmdev()
{
    dev_t dev;
    register struct inode *ip;

    ip = namei(uchar, 0);
    if (ip == NULL)
        return (NODEV);
    if ((ip->i_mode & IFMT) != IFBLK)
        u.u_error = ENOTBLK;
    dev = (dev_t)ip->i_un.i_rdev;
    // i_rdev comes off the disk, so a hostile mknod can make it negative
    if (major(dev) < 0 || major(dev) >= nblkdev)
        u.u_error = ENXIO;
    iput(ip);
    return (dev);
}
