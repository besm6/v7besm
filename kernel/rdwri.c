/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 1999 Robert Nordier. All rights reserved. */

// clang-format off
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/inode.h"
#include "sys/dir.h"
#include "sys/user.h"
#include "sys/buf.h"
#include "sys/conf.h"
// clang-format on

/*
 * Read the file corresponding to
 * the inode pointed at by the argument.
 * The actual read arguments are found
 * in the variables:
 *	u_base		core address for destination
 *	u_offset	byte offset in file
 *	u_count		number of bytes to read
 *	u_segflg	read to kernel/user/user I
 */
void readi(register struct inode *ip)
{
    struct buf *bp;
    dev_t dev;
    daddr_t lbn, bn;
    off_t diff;
    register int on, n;
    register int type;

    if (u.u_count == 0)
        return;
    if (u.u_offset < 0) {
        u.u_error = EINVAL;
        return;
    }
    ip->i_flag |= IACC;
    dev  = (dev_t)ip->i_un.i_rdev;
    type = ip->i_mode & IFMT;
    if (type == IFCHR || type == IFMPC) {
        (*cdevsw[major(dev)].d_read)(dev);
        return;
    }

    do {
        lbn = bn = u.u_offset >> BSHIFT;
        on       = u.u_offset & BMASK;
        n        = min((unsigned)(BSIZE - on), u.u_count);
        if (type != IFBLK && type != IFMPB) {
            diff = ip->i_size - u.u_offset;
            if (diff <= 0)
                return;
            if (diff < n)
                n = diff;
            bn = bmap(ip, bn, B_READ);
            if (u.u_error)
                return;
            dev = ip->i_dev;
        } else
            rablock = bn + 1;
        if ((long)bn < 0) {
            bp = geteblk();
            clrbuf(bp);
        } else if (ip->i_un.i_lastr + 1 == lbn)
            bp = breada(dev, bn, rablock);
        else
            bp = bread(dev, bn);
        ip->i_un.i_lastr = lbn;
        n                = min((unsigned)n, BSIZE - bp->b_resid);
        if (n != 0)
            iomove(bp->b_un.b_addr + on, n, B_READ);
        brelse(bp);
    } while (u.u_error == 0 && u.u_count != 0 && n > 0);
}

/*
 * Write the file corresponding to
 * the inode pointed at by the argument.
 * The actual write arguments are found
 * in the variables:
 *	u_base		core address for source
 *	u_offset	byte offset in file
 *	u_count		number of bytes to write
 *	u_segflg	write to kernel/user/user I
 */
void writei(register struct inode *ip)
{
    struct buf *bp;
    dev_t dev;
    daddr_t bn;
    register int n, on;
    register int type;

    if (u.u_offset < 0) {
        u.u_error = EINVAL;
        return;
    }
    dev  = (dev_t)ip->i_un.i_rdev;
    type = ip->i_mode & IFMT;
    if (type == IFCHR || type == IFMPC) {
        ip->i_flag |= IUPD | ICHG;
        (*cdevsw[major(dev)].d_write)(dev);
        return;
    }
    if (u.u_count == 0)
        return;

    do {
        bn = u.u_offset >> BSHIFT;
        on = u.u_offset & BMASK;
        n  = min((unsigned)(BSIZE - on), u.u_count);
        if (type != IFBLK && type != IFMPB) {
            bn = bmap(ip, bn, B_WRITE);
            if ((long)bn < 0)
                return;
            dev = ip->i_dev;
        }
        if (n == BSIZE)
            bp = getblk(dev, bn);
        else
            bp = bread(dev, bn);
        iomove(bp->b_un.b_addr + on, n, B_WRITE);
        if (u.u_error != 0)
            brelse(bp);
        else
            bdwrite(bp);
        if (u.u_offset > ip->i_size && (type == IFDIR || type == IFREG))
            ip->i_size = u.u_offset;
        ip->i_flag |= IUPD | ICHG;
    } while (u.u_error == 0 && u.u_count != 0);
}

/*
 * Return the logical maximum
 * of the 2 arguments.
 */
unsigned max(unsigned a, unsigned b)
{
    if (a > b)
        return (a);
    return (b);
}

/*
 * Return the logical minimum
 * of the 2 arguments.
 */
unsigned min(unsigned a, unsigned b)
{
    if (a < b)
        return (a);
    return (b);
}

/*
 * Move n bytes at byte location
 * &bp->b_un.b_addr[o] to/from (flag) the
 * user/kernel (u.segflg) area starting at u.base.
 * Update all the arguments by the number
 * of bytes moved.
 *
 * There are 2 algorithms,
 * if source address, dest address and count
 * are all even in a user copy,
 * then the machine language copyin/copyout
 * is called.
 * If not, its done byte-by-byte with
 * cpass and passc.
 */
void iomove(register caddr_t cp, register int n, int flag)
{
    register int t;

    if (n == 0)
        return;
    if (u.u_segflg != 1 && (n & (NBPW - 1)) == 0 && ((int)cp & (NBPW - 1)) == 0 &&
        ((int)u.u_base & (NBPW - 1)) == 0) {
        if (flag == B_WRITE)
            t = copyin(u.u_base, (caddr_t)cp, n);
        else
            t = copyout((caddr_t)cp, u.u_base, n);
        if (t) {
            u.u_error = EFAULT;
            return;
        }
        u.u_base += n;
        u.u_offset += n;
        u.u_count -= n;
        return;
    } /* XXX even addresses */
    if (flag == B_WRITE) {
        do {
            if ((t = cpass()) < 0)
                return;
            *cp++ = t;
        } while (--n);
    } else
        do {
            if (passc(*cp++) < 0)
                return;
        } while (--n);
}
