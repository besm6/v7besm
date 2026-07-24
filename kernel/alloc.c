// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// clang-format off
#include "sys/types.h"
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/mount.h"
#include "sys/filsys.h"
#include "sys/fblk.h"
#include "sys/conf.h"
#include "sys/buf.h"
#include "sys/inode.h"
#include "sys/ino.h"
#include "sys/dir.h"
#include "sys/user.h"
// clang-format on

typedef struct fblk *FBLKP;

int updlock; // lock for sync

int badblock(register struct filsys *fp, daddr_t bn, dev_t dev);
int sbcheck(register struct filsys *fp, dev_t dev);

// alloc will obtain the next available
// free disk block from the free list of
// the specified device.
// The super block has up to NICFREE remembered
// free blocks; the last of these is read to
// obtain NICFREE more . . .
//
// no space on dev x/y -- when
// the free list is exhausted.
struct buf *alloc(dev_t dev)
{
    daddr_t bno;
    register struct filsys *fp;
    register struct buf *bp;

    fp = getfs(dev);
    while (fp->s_flock)
        sleep((chan_t)&fp->s_flock, PINOD);
    do {
        if (fp->s_nfree <= 0)
            goto nospace;
        if (fp->s_nfree > NICFREE) {
            prdev("Bad free count", dev);
            goto nospace;
        }
        bno = fp->s_free[--fp->s_nfree];
        if (bno == 0)
            goto nospace;
    } while (badblock(fp, bno, dev));
    if (fp->s_nfree <= 0) {
        fp->s_flock++;
        bp = bread(dev, bno);
        if ((bp->b_flags & B_ERROR) == 0) {
            fp->s_nfree = ((FBLKP)(bp->b_addr))->df_nfree;
            wcopy((caddr_t)((FBLKP)(bp->b_addr))->df_free, (caddr_t)fp->s_free,
                  btow(sizeof(fp->s_free)));
        }
        brelse(bp);
        fp->s_flock = 0;
        wakeup((chan_t)&fp->s_flock);
        if (fp->s_nfree <= 0)
            goto nospace;
    }
    bp = getblk(dev, bno);
    clrbuf(bp);
    fp->s_fmod = 1;
    return (bp);

nospace:
    fp->s_nfree = 0;
    prdev("no space", dev);
    u.u_error = ENOSPC;
    return (NULL);
}

// place the specified disk block
// back on the free list of the
// specified device.
void free(dev_t dev, daddr_t bno)
{
    register struct filsys *fp;
    register struct buf *bp;

    fp         = getfs(dev);
    fp->s_fmod = 1;
    while (fp->s_flock)
        sleep((chan_t)&fp->s_flock, PINOD);
    if (badblock(fp, bno, dev))
        return;
    if (fp->s_nfree <= 0) {
        fp->s_nfree   = 1;
        fp->s_free[0] = 0;
    }
    if (fp->s_nfree >= NICFREE) {
        fp->s_flock++;
        bp = getblk(dev, bno);
        // getblk() does not read, so the buffer still holds whatever it held last.
        // Only 1 + NICFREE of its 512 words are about to be filled in, and without
        // this the remaining 191 would be written to the disk as they stand -- old
        // kernel memory, on every chain block the filesystem ever grows.
        clrbuf(bp);
        ((FBLKP)(bp->b_addr))->df_nfree = fp->s_nfree;
        wcopy((caddr_t)fp->s_free, (caddr_t)((FBLKP)(bp->b_addr))->df_free,
              btow(sizeof(fp->s_free)));
        fp->s_nfree = 0;
        bwrite(bp);
        fp->s_flock = 0;
        wakeup((chan_t)&fp->s_flock);
    }
    fp->s_free[fp->s_nfree++] = bno;
    fp->s_fmod                = 1;
}

// Check that a block number is in the
// range between the I list and the size
// of the device.
// This is used mainly to check that a
// garbage file system has not been mounted.
//
// bad block on dev x/y -- not in range
int badblock(register struct filsys *fp, daddr_t bn, dev_t dev)
{
    if (bn < fp->s_isize || bn >= fp->s_fsize) {
        prdev("bad block", dev);
        return (1);
    }
    return (0);
}

// Is this block plausibly a superblock for THIS kernel?  Returns 0 if so, 1 if not.
//
// v7 has no such test: iinit() and smount() copy block 1 in and believe it, so a
// garbage block mounts silently and the first symptom is badblock(), or getfs()'s
// "bad count" -- which "repairs" the superblock by zeroing both counts, turning
// garbage into a plausible-looking full filesystem.  iinit() even sets the system
// clock from an unchecked s_time.
//
// The geometry words are not ceremony.  These constants are actively in flux in
// this port -- INOPB went 8 -> 32 and NADDR 13 -> 8 one commit ago -- and an image
// built by a mkfs one generation out of step would otherwise mount perfectly well
// and read every inode from the wrong offset.
int sbcheck(register struct filsys *fp, dev_t dev)
{
    if (fp->s_magic != FS_MAGIC) {
        prdev("not a filesystem", dev);
        return (1);
    }
    if (fp->s_bsize != BSIZEW || fp->s_inopb != INOPB || fp->s_naddr != NADDR) {
        prdev("filesystem geometry mismatch", dev);
        return (1);
    }
    // The i-list starts just past the superblock and must end before the volume
    // does.  s_isize bounds ialloc()'s scan loop, so a garbage value here is a
    // runaway read, not merely a wrong answer.
    if (fp->s_isize <= SUPERB || fp->s_isize >= fp->s_fsize) {
        prdev("bad filesystem size", dev);
        return (1);
    }
    if (fp->s_nfree < 0 || fp->s_nfree > NICFREE || fp->s_ninode < 0 || fp->s_ninode > NICINOD) {
        prdev("bad free count", dev);
        return (1);
    }
    return (0);
}

// Allocate an unused I node
// on the specified device.
// Used with file creation.
// The algorithm keeps up to
// NICINOD spare I nodes in the
// super block. When this runs out,
// a linear search through the
// I list is instituted to pick
// up NICINOD more.
struct inode *ialloc(dev_t dev)
{
    register struct filsys *fp;
    register struct buf *bp;
    register struct inode *ip;
    int i;
    struct dinode *dp;
    ino_t ino;
    daddr_t adr;

    fp = getfs(dev);
    while (fp->s_ilock)
        sleep((chan_t)&fp->s_ilock, PINOD);
loop:
    if (fp->s_ninode > 0) {
        ino = fp->s_inode[--fp->s_ninode];
        if (ino < ROOTINO)
            goto loop;
        ip = iget(dev, ino);
        if (ip == NULL)
            return (NULL);
        if (ip->i_mode == 0) {
            for (i = 0; i < NADDR; i++)
                ip->i_un.i_addr[i] = 0;
            fp->s_fmod = 1;
            return (ip);
        }
        // Inode was allocated after all.
        // Look some more.
        iput(ip);
        goto loop;
    }
    fp->s_ilock++;
    ino = 1;
    for (adr = SUPERB + 1; adr < fp->s_isize; adr++) {
        bp = bread(dev, adr);
        if (bp->b_flags & B_ERROR) {
            brelse(bp);
            ino += INOPB;
            continue;
        }
        dp = (struct dinode *)bp->b_addr;
        for (i = 0; i < INOPB; i++) {
            if (dp->di_mode != 0)
                goto cont;
            for (ip = &inode[0]; ip < &inode[NINODE]; ip++)
                if (dev == ip->i_dev && ino == ip->i_number)
                    goto cont;
            fp->s_inode[fp->s_ninode++] = ino;
            if (fp->s_ninode >= NICINOD)
                break;
        cont:
            ino++;
            dp++;
        }
        brelse(bp);
        if (fp->s_ninode >= NICINOD)
            break;
    }
    fp->s_ilock = 0;
    wakeup((chan_t)&fp->s_ilock);
    if (fp->s_ninode > 0)
        goto loop;
    prdev("Out of inodes", dev);
    u.u_error = ENOSPC;
    return (NULL);
}

// Free the specified I node
// on the specified device.
// The algorithm stores up
// to NICINOD I nodes in the super
// block and throws away any more.
void ifree(dev_t dev, ino_t ino)
{
    register struct filsys *fp;

    fp = getfs(dev);
    if (fp->s_ilock)
        return;
    if (fp->s_ninode >= NICINOD)
        return;
    fp->s_inode[fp->s_ninode++] = ino;
    fp->s_fmod                  = 1;
}

// getfs maps a device number into
// a pointer to the incore super
// block.
// The algorithm is a linear
// search through the mount table.
// A consistency check of the
// in core free-block and i-node
// counts.
//
// bad count on dev x/y -- the count
// 	check failed. At this point, all
// 	the counts are zeroed which will
// 	almost certainly lead to "no space"
// 	diagnostic
// panic: no fs -- the device is not mounted.
// 	this "cannot happen"
struct filsys *getfs(dev_t dev)
{
    register struct mount *mp;
    register struct filsys *fp;

    for (mp = &mount[0]; mp < &mount[NMOUNT]; mp++)
        if (mp->m_bufp != NULL && mp->m_dev == dev) {
            fp = (struct filsys *)mp->m_bufp->b_addr;
            if (fp->s_nfree > NICFREE || fp->s_ninode > NICINOD) {
                prdev("bad count", dev);
                fp->s_nfree  = 0;
                fp->s_ninode = 0;
            }
            return (fp);
        }
    panic("no fs");
    return (NULL);
}

// update is the internal name of
// 'sync'. It goes through the disk
// queues to initiate sandbagged IO;
// goes through the I nodes to write
// modified nodes; and it goes through
// the mount table to initiate modified
// super blocks.
void update()
{
    register struct inode *ip;
    register struct mount *mp;
    register struct buf *bp;
    struct filsys *fp;

    if (updlock)
        return;
    updlock++;
    for (mp = &mount[0]; mp < &mount[NMOUNT]; mp++)
        if (mp->m_bufp != NULL) {
            fp = (struct filsys *)mp->m_bufp->b_addr;
            if (fp->s_fmod == 0 || fp->s_ilock != 0 || fp->s_flock != 0 || fp->s_ronly != 0)
                continue;
            bp = getblk(mp->m_dev, SUPERB);
            if (bp->b_flags & B_ERROR)
                continue;
            fp->s_fmod = 0;
            fp->s_time = time;
            wcopy((caddr_t)fp, bp->b_addr, BSIZEW);
            bwrite(bp);
        }
    for (ip = &inode[0]; ip < &inode[NINODE]; ip++)
        if ((ip->i_flag & ILOCK) == 0 && ip->i_count) {
            ip->i_flag |= ILOCK;
            ip->i_count++;
            iupdat(ip, &time, &time);
            iput(ip);
        }
    updlock = 0;
    bflush(NODEV);
}
