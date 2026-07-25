// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// clang-format off
#include "sys/types.h"
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/inode.h"
#include "sys/dir.h"
#include "sys/user.h"
#include "sys/buf.h"
#include "sys/conf.h"
// clang-format on

// Read the file corresponding to
// the inode pointed at by the argument.
// The actual read arguments are found
// in the variables:
// 	u_base		core address for destination
// 	u_offset	byte offset in file
// 	u_count		number of bytes to read
// 	u_segflg	read to kernel/user/user I
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
        // A divide and a remainder, not a shift and a mask: BSIZE is 3072 bytes and
        // 3072 is not a power of two.  One b$div per pass of this loop -- that is,
        // per block -- which is noise beside the bread() below.  `on' stays a BYTE
        // offset: b_addr is a word pointer, so the `+ on' below is byte arithmetic on
        // the caddr_t it is cast to first, which starts at the block's first byte.
        lbn = bn = u.u_offset / BSIZE;
        on       = u.u_offset % BSIZE;
        n        = min(BSIZE - on, u.u_count);
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
        n                = min(n, BSIZE - wtob(bp->b_resid));
        if (n != 0)
            iomove((caddr_t)bp->b_addr + on, n, B_READ);
        brelse(bp);
    } while (u.u_error == 0 && u.u_count != 0 && n > 0);
}

// Write the file corresponding to
// the inode pointed at by the argument.
// The actual write arguments are found
// in the variables:
// 	u_base		core address for source
// 	u_offset	byte offset in file
// 	u_count		number of bytes to write
// 	u_segflg	write to kernel/user/user I
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
        // Divide and remainder, not shift and mask; see readi() above.
        bn = u.u_offset / BSIZE;
        on = u.u_offset % BSIZE;
        n  = min(BSIZE - on, u.u_count);
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
        iomove((caddr_t)bp->b_addr + on, n, B_WRITE);
        if (u.u_error != 0)
            brelse(bp);
        else
            bdwrite(bp);
        if (u.u_offset > ip->i_size && (type == IFDIR || type == IFREG))
            ip->i_size = u.u_offset;
        ip->i_flag |= IUPD | ICHG;
    } while (u.u_error == 0 && u.u_count != 0);
}

// Return the logical maximum
// of the 2 arguments.
int max(int a, int b)
{
    if (a > b)
        return (a);
    return (b);
}

// Return the logical minimum
// of the 2 arguments.
int min(int a, int b)
{
    if (a < b)
        return (a);
    return (b);
}

// Move n bytes at byte location
// &bp->b_addr[o] to/from (flag) the
// user/kernel (u.segflg) area starting at u.base.
// Update all the arguments by the number
// of bytes moved.
//
// There are 2 algorithms.  If source address, dest address and count are all WORD-ALIGNED
// in a user copy, the machine-language copyin/copyout is called; if not, it is done
// byte-by-byte with cpass and passc.
//
// THE ALIGNMENT TEST IS NOT v7's, AND v7's WAS A SILENT DATA-CORRUPTION BUG HERE.  The
// original asked `(n & (NBPW-1)) == 0 && ((int)cp & (NBPW-1)) == 0 && ...' -- a low-bit
// mask, which works on a PDP-11 where NBPW is 2 and an address is a byte address.  Neither
// half survives the move to this machine:
//
//   - NBPW is 6, which is NOT A POWER OF TWO, so `n & 5' is not `n % 6'.  It accepts 24 and
//     26 alike and rejects 6 and 18.
//   - a caddr_t is a FAT POINTER, and its byte offset lives in bits 47-45 (param.h,
//     doc/Besm6_Data_Representation.md section 7).  `(int)cp & 5' therefore tests bits 1
//     and 3 of the WORD ADDRESS and cannot see the byte offset at all.
//
// So the guard passed on unaligned buffers roughly one time in eight, and copyin/copyout --
// which are WORD-ONLY, masking the pointer with `aax #077777' and dropping the byte offset
// (usermem.S says so in its header) -- then wrote n/6 whole words at the word the pointer
// happened to lie in.  The result is a write whose data lands up to five bytes early and
// whose last bytes are never written at all: a stretch of the file shifted, then zeros.
//
// It is deterministic, it is rare enough to look like anything but an alignment bug, and it
// was invisible until user programs with byte-granular stdio buffers ran on the image --
// kernel/test/libtest (task 25c) found it in gen, strings, sbrkt and stdiot on its first
// run.  kernel/TODO.md task 28 listed this as a PERFORMANCE bug ("correctness is
// unaffected"); it was not.
//
// The test below is the one the fast path actually needs, and it is what usermem.S's header
// says its callers guarantee: a real modulo, and both pointers standing on byte #0 of their
// word.  ptrbyte() yields the shift field, in which 5 IS the word's first byte.  The `% NBPW'
// costs one b$div per block, against 3072 bytes moved.
void iomove(register caddr_t cp, register int n, int flag)
{
    register int t;

    if (n == 0)
        return;
    if (u.u_segflg != 1 && n % NBPW == 0 && ptrbyte(cp) == 5 && ptrbyte(u.u_base) == 5) {
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
    }
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
