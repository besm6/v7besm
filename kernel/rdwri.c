// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

#include "sys/buf.h"
#include "sys/conf.h"
#include "sys/dir.h"
#include "sys/inode.h"
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/types.h"
#include "sys/user.h"

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
// There are 2 algorithms.  A user copy goes to copyinb/copyoutb (ucopy.c), which take the
// byte offsets apart and hand the machine-language copyin/copyout whole words; a KERNEL copy
// -- u_segflg 1, where u_base is a kernel address and there is no boundary to cross -- is
// done byte-by-byte with cpass and passc.
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
// run.  The task that fixed the alignment TEST listed this as a PERFORMANCE bug ("correctness
// is unaffected"); it was not.
//
// THE ALIGNMENT TEST IS GONE FROM HERE ENTIRELY, and that is the point of the bulk path.  It
// used to be the fast path's admission criterion; it is now a decision copyinb/copyoutb make
// for themselves, one word at a time, where kernel/test/umem can drive it directly.  This
// routine no longer knows what a phase is.
//
// WHAT THAT COST WAS, measured before the change with a counter on each arm (the three are
// still there, in ucopy.c, and libtest asserts on them).  Bytes through iomove():
//
//                     bulk             byte, in phase    byte, out of phase
//      session      361,296 (97.4%)      6,736 (1.8%)       2,894 (0.8%)
//      libtest    1,088,562 (86.8%)     70,231 (5.6%)      94,805 (7.6%)
//
// So the premise the task was written on -- "the slow path is the ordinary one for user I/O"
// -- was wrong: BUFSIZ is 3072, which is BSIZE, so an ordinary stdio transfer is already
// block-aligned and word-aligned and always took the bulk arm.  What the byte arm carries is
// the REMAINDER: the tail of a file, and every program that reads or writes in units that are
// not multiples of six.  It is 13% of the bytes and far more than 13% of the time, since a
// byte on that arm costs a useracc() walk and a mode-toggle bracket of its own.
//
// Both halves of that byte arm are now reached, and reached completely: at most ten byte
// operations per 3072-byte block instead of 3072.  The in-phase half went first (task 28,
// copyin/copyout on the whole middle); the out-of-phase half followed (task 36), where the
// middle is a funnel shift written in C -- `unsigned' is one 48-bit word, so two shifts and an
// `or' join the tail of one source word to the head of the next, and the boundary is crossed
// once per six bytes rather than six times.  What is left on the byte arm is the two ends of a
// transfer, five bytes each at the worst, and nioedge is what counts them.
//
// EFAULT ACCOUNTING CHANGED, deliberately.  The byte arm used to advance u_base/u_count/
// u_offset per byte, so a fault left a partial account; copyinb/copyoutb validate the whole
// range up front and are all-or-nothing, so nothing is advanced at all.  Nothing observes it
// -- rdwr() discards r_val1 once u_error is set -- and the new behaviour is the one
// usermem.S's header already promised for copyin/copyout.
//
// AND ONE LATENT INCONSISTENCY GOES WITH IT.  u_segflg 2 (xalloc(), text.c) is a USER access,
// and passed the old `!= 1' guard as one -- but if the block had been unaligned it would have
// fallen through to cpass()/passc(), which test `if (u.u_segflg)' and would have done a
// KERNEL store into the user's address as if it were a pointer.  It never happened only
// because a_const and a_text are multiples of six.  Now 2 never reaches that path at all.
void iomove(register caddr_t cp, register int n, int flag)
{
    register int t;

    if (n == 0)
        return;
    if (u.u_segflg != 1) {
        if (flag == B_WRITE)
            t = copyinb(u.u_base, (caddr_t)cp, n);
        else
            t = copyoutb((caddr_t)cp, u.u_base, n);
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
