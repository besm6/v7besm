/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 1999 Robert Nordier. All rights reserved. */

// clang-format off
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/dir.h"
#include "sys/user.h"
#include "sys/filsys.h"
#include "sys/mount.h"
#include "sys/map.h"
#include "sys/proc.h"
#include "sys/inode.h"
#include "sys/seg.h"
#include "sys/conf.h"
#include "sys/buf.h"
// clang-format on

extern int kend;

time_t time; /* time in sec from 1970 */
int nblkdev;
int dk_busy;
long dk_numb[3];
long dk_wds[3];
long dk_time[32];
struct mount mount[NMOUNT];
struct inode *rootdir; /* pointer to inode of root directory */

/*
 * Initialization code.
 * Called from cold start routine as
 * soon as a stack and segmentation
 * have been established.
 * Functions:
 *	clear and free user core
 *	turn on clock
 *	hand craft 0th process
 *	call all initialization routines
 *	fork - process 0 to schedule
 *	     - process 1 execute bootstrap
 *
 * loop at low address in user mode -- /etc/init
 *	cannot be executed.
 */
main()
{
    startup();
    /*
     * set up system process
     */

    proc[0].p_addr = btoc(kend) + 7; /* XXX */
    proc[0].p_size = USIZE;
    proc[0].p_stat = SRUN;
    proc[0].p_flag |= SLOAD | SSYS;
    proc[0].p_nice = NZERO;
    u.u_procp      = &proc[0];
    u.u_cmask      = CMASK;

    /*
     * Initialize devices and
     * set up 'known' i-nodes
     */

    clkstart();
    cinit();
    binit();
    iinit();
    rootdir = iget(rootdev, (ino_t)ROOTINO);
    rootdir->i_flag &= ~ILOCK;
    u.u_cdir = iget(rootdev, (ino_t)ROOTINO);
    u.u_cdir->i_flag &= ~ILOCK;
    u.u_rdir = NULL;

    /*
     * make init process
     * enter scheduling loop
     * with system process
     */

    if (newproc()) {
        expand(USIZE + (int)btoc(szicode));
        estabur((unsigned)0, btoc(szicode), (unsigned)0, 0, RO);
        copyout((caddr_t)icode, (caddr_t)0, szicode);
        /*
         * Return goes to loc. 0 of user init
         * code just copied out.
         */
        return;
    }
    sched();
}

/*
 * iinit is called once (from main)
 * very early in initialization.
 * It reads the root's super block
 * and initializes the current date
 * from the last modified date.
 *
 * panic: iinit -- cannot read the super
 * block. Usually because of an IO error.
 */
iinit()
{
    register struct buf *cp, *bp;
    register struct filsys *fp;

    (*bdevsw[major(rootdev)].d_open)(rootdev, 1);
    bp = bread(rootdev, SUPERB);
    cp = geteblk();
    if (u.u_error)
        panic("iinit");
    bcopy(bp->b_un.b_addr, cp->b_un.b_addr, sizeof(struct filsys));
    brelse(bp);
    mount[0].m_bufp = cp;
    mount[0].m_dev  = rootdev;
    fp              = cp->b_un.b_filsys;
    fp->s_flock     = 0;
    fp->s_ilock     = 0;
    fp->s_ronly     = 0;
    if (time == 0)
        time = fp->s_time;
}

/*
 * This is the set of buffers proper, whose heads
 * were declared in buf.h.  There can exist buffer
 * headers not pointing here that are used purely
 * as arguments to the I/O routines to describe
 * I/O to be done-- e.g. swbuf for
 * swapping.
 */
char buffers[NBUF][BSIZE];

/*
 * Initialize the buffer I/O system by freeing
 * all buffers and setting all device buffer lists to empty.
 */
binit()
{
    register struct buf *bp;
    register struct buf *dp;
    register int i;
    struct bdevsw *bdp;

    bfreelist.b_forw = bfreelist.b_back = bfreelist.av_forw = bfreelist.av_back = &bfreelist;
    for (i = 0; i < NBUF; i++) {
        bp                       = &buf[i];
        bp->b_dev                = NODEV;
        bp->b_un.b_addr          = buffers[i];
        bp->b_back               = &bfreelist;
        bp->b_forw               = bfreelist.b_forw;
        bfreelist.b_forw->b_back = bp;
        bfreelist.b_forw         = bp;
        bp->b_flags              = B_BUSY;
        brelse(bp);
    }
    for (bdp = bdevsw; bdp->d_open; bdp++) {
        dp = bdp->d_tab;
        if (dp) {
            dp->b_forw = dp;
            dp->b_back = dp;
        }
        nblkdev++;
    }
}
