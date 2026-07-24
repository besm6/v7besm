// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// clang-format off
#include "sys/types.h"
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

time_t time; // time in sec from 1970
int nblkdev;
int dk_busy;
int dk_numb[3];
int dk_wds[3];
int dk_time[32];
struct mount mount[NMOUNT];
struct inode *rootdir; // pointer to inode of root directory

void iinit(void);
void binit(void);
void drainbrz(void); // brz.s

// Initialization code.
// Called from cold start routine as
// soon as a stack and segmentation
// have been established.
// Functions:
// 	clear and free user core
// 	turn on clock
// 	hand craft 0th process
// 	call all initialization routines
// 	fork - process 0 to schedule
// 	     - process 1 execute bootstrap
//
// loop at low address in user mode -- /etc/init
// 	cannot be executed.
void main()
{
    register int nw, nd; // the icode's size, and the data region that has to hold it

    startup();

    // set up system process

    proc[0].p_addr = NPAGE * PGSZ;   // 0100000: the first free word
    uhome          = proc[0].p_addr; // the live u-area at UBASE is proc[0]'s
    proc[0].p_size = USIZE;
    proc[0].p_stat = SRUN;
    proc[0].p_flag |= SLOAD | SSYS;
    proc[0].p_nice = NZERO;
    u.u_procp      = &proc[0];
    u.u_cmask      = CMASK;

    // Initialize devices and
    // set up 'known' i-nodes

    intrinit(); // arm МГРП before anything can call spl0() and open БлПр
    clkstart();
    cinit();
    binit();
    iinit();
    rootdir = iget(rootdev, (ino_t)ROOTINO);
    rootdir->i_flag &= ~ILOCK;
    u.u_cdir = iget(rootdev, (ino_t)ROOTINO);
    u.u_cdir->i_flag &= ~ILOCK;
    u.u_rdir = NULL;

    // make init process
    // enter scheduling loop
    // with system process

    if (newproc()) {
        // Process 1: give it the icode and return, which _start (kernel/besm6.S) turns into
        // the first entry into user mode.
        //
        // THE ICODE IS COPIED TO THE ADDRESS IT WAS LINKED AT -- src and dst are the same
        // number, and only the side of БлП they are read and written on tells them apart --
        // so its own labels are correct in the user's space too.  besm6.S says why that is
        // the only way it can name anything.  The data region therefore has to cover word 0
        // up through the end of the block, not just the block's length.
        nw = eicode - icode;                       // words of icode (sizeof(int) == 1 word)
        nd = pground(ptrword((caddr_t)icode) + nw);

        expand(USIZE + nd + SSIZE);
        estabur(0, nd, SSIZE, 0, RO);
        copyout((caddr_t)icode, (caddr_t)icode, wtob(nw));

        // Drain the БРЗ write cache before anything can FETCH what we just stored.  The
        // copyout stores were made MAPPED and are still in the cache, and the instruction
        // path does not look there: it reads memory directly (besm6_mmu.c, mmu_prefetch vs
        // mmu_store).  A data read WOULD find them -- it is only the fetch that has to be fed.
        //
        // Measured, with `set mmu cache' and a breakpoint on the instruction after the
        // copyout: of the nine icode words, TWO had reached memory and SEVEN were still in
        // BRZ0-BRZ7, with the user's page reading zero where they belong.  The boot survives
        // without this call today only because main()'s own epilogue and _start's tail then
        // issue enough kernel stores to evict all eight lines by age before the выпр -- an
        // accident of instruction count, not a guarantee, and one a bigger icode or a
        // different epilogue would take away.  Nothing in the suite bites it (see
        // test/boot.ini.in); the argument is the measurement above.
        //
        // exec() gets the same drain for free from the estabur() that follows its readi();
        // this path has no estabur() behind its copyout(), so it drains for itself.
        drainbrz();
        return;
    }
    sched();
}

// iinit is called once (from main)
// very early in initialization.
// It reads the root's super block
// and initializes the current date
// from the last modified date.
//
// panic: iinit -- cannot read the super
// block. Usually because of an IO error.
// panic: no root fs -- block 1 was read, but
// it is not a superblock this kernel can use.
void iinit()
{
    register struct buf *cp, *bp;
    register struct filsys *fp;

    (*bdevsw[major(rootdev)].d_open)(rootdev, 1);
    bp = bread(rootdev, SUPERB);
    cp = geteblk();
    if (u.u_error)
        panic("iinit");
    // Check it BEFORE it is installed in mount[0] and before the clock is set from
    // s_time below: v7 did neither, so a garbage root ran the system on a garbage
    // date and was only noticed once something tried to allocate.
    if (sbcheck((struct filsys *)bp->b_addr, rootdev))
        panic("no root fs");
    // btow(sizeof(struct filsys)) is BSIZEW now that the superblock is exactly one
    // block, which is what makes this agree with update()'s BSIZEW write-back.  It
    // used to copy 165 words into a buffer whose other 347 update() then wrote to
    // the disk unread.  Left derived rather than spelled BSIZEW so it stays honest
    // if the struct ever changes; filsys.h asserts the two are the same.
    wcopy(bp->b_addr, cp->b_addr, btow(sizeof(struct filsys)));
    brelse(bp);
    mount[0].m_bufp = cp;
    mount[0].m_dev  = rootdev;
    fp              = (struct filsys *)cp->b_addr;
    fp->s_flock     = 0;
    fp->s_ilock     = 0;
    fp->s_ronly     = 0;
    if (time == 0)
        time = fp->s_time;
    printf ("root size = %d kbytes\n", fp->s_fsize * BSIZE / 1024);
}

// This is the set of buffers proper, whose heads
// were declared in buf.h.  There can exist buffer
// headers not pointing here that are used purely
// as arguments to the I/O routines to describe
// I/O to be done-- e.g. swbuf for
// swapping.
extern int buffers[NBUF][BSIZEW];

// Initialize the buffer I/O system by freeing
// all buffers and setting all device buffer lists to empty.
void binit()
{
    register struct buf *bp;
    register struct buf *dp;
    register int i;
    struct bdevsw *bdp;

    bfreelist.b_forw = bfreelist.b_back = bfreelist.av_forw = bfreelist.av_back = &bfreelist;
    for (i = 0; i < NBUF; i++) {
        bp                       = &buf[i];
        bp->b_dev                = NODEV;
        bp->b_addr               = buffers[i];
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
