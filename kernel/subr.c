/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 1999 Robert Nordier. All rights reserved. */

// clang-format off
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/conf.h"
#include "sys/inode.h"
#include "sys/dir.h"
#include "sys/user.h"
#include "sys/buf.h"
// clang-format on

daddr_t rablock; /* block to be read ahead */

/*
 * Bmap defines the structure of file system storage
 * by returning the physical block number on a device given the
 * inode and the logical block number in a file.
 * When convenient, it also leaves the physical
 * block number of the next block of the file in rablock
 * for use in read-ahead.
 */
daddr_t bmap(register struct inode *ip, daddr_t bn, int rwflg)
{
    register int i;
    struct buf *bp, *nbp;
    int j, sh;
    daddr_t nb, *bap;
    dev_t dev;

    if (bn < 0) {
        u.u_error = EFBIG;
        return ((daddr_t)0);
    }
    dev     = ip->i_dev;
    rablock = 0;

    /*
     * blocks 0..NADDR-4 are direct blocks
     */
    if (bn < NADDR - 3) {
        i  = bn;
        nb = ip->i_un.i_addr[i];
        if (nb == 0) {
            if (rwflg == B_READ || (bp = alloc(dev)) == NULL)
                return ((daddr_t)-1);
            nb = bp->b_blkno;
            bdwrite(bp);
            ip->i_un.i_addr[i] = nb;
            ip->i_flag |= IUPD | ICHG;
        }
        if (i < NADDR - 4)
            rablock = ip->i_un.i_addr[i + 1];
        return (nb);
    }

    /*
     * addresses NADDR-3, NADDR-2, and NADDR-1
     * have single, double, triple indirect blocks.
     * the first step is to determine
     * how many levels of indirection.
     */
    sh = 0;
    nb = 1;
    bn -= NADDR - 3;
    for (j = 3; j > 0; j--) {
        sh += NSHIFT;
        nb <<= NSHIFT;
        if (bn < nb)
            break;
        bn -= nb;
    }
    if (j == 0) {
        u.u_error = EFBIG;
        return ((daddr_t)0);
    }

    /*
     * fetch the address from the inode
     */
    nb = ip->i_un.i_addr[NADDR - j];
    if (nb == 0) {
        if (rwflg == B_READ || (bp = alloc(dev)) == NULL)
            return ((daddr_t)-1);
        nb = bp->b_blkno;
        bdwrite(bp);
        ip->i_un.i_addr[NADDR - j] = nb;
        ip->i_flag |= IUPD | ICHG;
    }

    /*
     * fetch through the indirect blocks
     */
    for (; j <= 3; j++) {
        bp = bread(dev, nb);
        if (bp->b_flags & B_ERROR) {
            brelse(bp);
            return ((daddr_t)0);
        }
        bap = bp->b_un.b_daddr;
        sh -= NSHIFT;
        i  = (bn >> sh) & NMASK;
        nb = bap[i];
        if (nb == 0) {
            if (rwflg == B_READ || (nbp = alloc(dev)) == NULL) {
                brelse(bp);
                return ((daddr_t)-1);
            }
            nb = nbp->b_blkno;
            bdwrite(nbp);
            bap[i] = nb;
            bdwrite(bp);
        } else
            brelse(bp);
    }

    /*
     * calculate read-ahead.
     */
    if (i < NINDIR - 1)
        rablock = bap[i + 1];
    return (nb);
}

/*
 * Pass back  c  to the user at his location u_base;
 * update u_base, u_count, and u_offset.  Return -1
 * on the last character of the user's read.
 * u_base is in the user address space unless u_segflg is set.
 */
int passc(register int c)
{
    if (u.u_segflg)
        *u.u_base = c;
    else if (subyte(u.u_base, c) < 0) {
        u.u_error = EFAULT;
        return (-1);
    }
    u.u_count--;
    u.u_offset++;
    u.u_base++;
    return (u.u_count == 0 ? -1 : 0);
}

/*
 * Pick up and return the next character from the user's
 * write call at location u_base;
 * update u_base, u_count, and u_offset.  Return -1
 * when u_count is exhausted.  u_base is in the user's
 * address space unless u_segflg is set.
 */
int cpass()
{
    register int c;

    if (u.u_count == 0)
        return (-1);
    if (u.u_segflg)
        c = *u.u_base;
    else if ((c = fubyte(u.u_base)) < 0) {
        u.u_error = EFAULT;
        return (-1);
    }
    u.u_count--;
    u.u_offset++;
    u.u_base++;
    return (c & 0377);
}

/*
 * Routine which sets a user error; placed in
 * illegal entries in the bdevsw and cdevsw tables.
 */
void nodev()
{
    u.u_error = ENODEV;
}

/*
 * Null routine; placed in insignificant entries
 * in the bdevsw and cdevsw tables.
 */
void nulldev()
{
}

void nullopen(dev_t dev, int flag)
{
}

void nullclose(dev_t dev, int flag)
{
}

void nullrw(dev_t dev)
{
}

void nullioctl(dev_t dev, int cmd, caddr_t addr, int flag)
{
}

void nulldstop(struct tty *tp)
{
}

void nulltclose(struct tty *tp)
{
}

void nulltioctl(int com, struct tty *tp, caddr_t addr)
{
}
