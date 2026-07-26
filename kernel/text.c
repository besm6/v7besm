// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// clang-format off
#include "sys/types.h"
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/map.h"
#include "sys/dir.h"
#include "sys/user.h"
#include "sys/proc.h"
#include "sys/text.h"
#include "sys/inode.h"
#include "sys/buf.h"
#include "sys/seg.h"
// clang-format on

void xexpand(register struct text *xp);
void xccdec(register struct text *xp);
void xuntext(register struct text *xp);

// The swapper's traffic counters, declared in <sys/systm.h>, defined here because four of
// the five events are this file's.  They are what a load test asserts on; see the comment
// over them in the header for why a test needs them at all.
int nswapout;
int ntextin;
int ntextout;
int ntextjoin;

// Swap out process p.
// The ff flag causes its core to be freed--
// it may be off when called to create an image for a
// child process in newproc.
// Os is the old size of the data area of the process,
// and is supplied during core expansion swaps.
//
// panic: out of swap space
//
// ---------------------------------------------------------------------------------------
// THE U-AREA INVARIANT.  All of it, in one place, because scattering it is a footgun.
//
// On this machine the live u-area is a fixed PHYSICAL page at UBASE, not a page mapped at a
// fixed virtual address, so `u' is NOT part of the current process's image: the copy sitting
// in the image at p_addr is stale between context switches.  `uhome' names the image the
// live u-area belongs to, and resume() (kernel/switch.s) keeps the two in step -- it flushes
// the outgoing u to `uhome' and loads the incoming one, whenever they differ.
//
// Which means ANYTHING ELSE THAT READS OR FREES THE CURRENT PROCESS'S IMAGE has to say so.
// There are five such places, and only two of them are obvious:
//
//   1. HERE, below.  swap() DMAs the image straight out of physical memory, so an unflushed
//      u page would put a stale struct user -- stale u_upt, stale labels, stale kernel stack
//      -- on the disk.  The test is `p->p_addr == uhome', NOT "p is the current process":
//      newproc() calls xswap() on the CHILD, whose p_addr is still the parent's image.
//      Doing it here rather than at the four call sites (sched(), newproc(), expand(),
//      xexpand()) is what makes those four correct without auditing them.
//
//   2. ... and if `ff' says the core is about to be freed, the live u-area is left with no
//      home at all.  Saying NOUHOME is not tidiness: the next resume() would otherwise flush
//      1024 words into core that malloc() may already have handed to somebody else.
//
//   3. newproc(), which copies the parent's image to build the child's (kernel/slp.c).
//   4. expand(), which copies the image to a new address -- and skips the u page, because
//      the live copy is authoritative and it sets uhome to the new address instead.
//   5. exit(), which frees the image outright (kernel/sys1.c).  Same hazard as 2.
//
// A SIXTH, added later and forgotten, will be a very confusing bug.  See kernel/TODO.md.
// ---------------------------------------------------------------------------------------
void xswap(register struct proc *p, int ff, int os)
{
    register int a, n;

    if (os == 0)
        os = p->p_size;
    a = malloc(swapmap, wtodb(p->p_size));
    if (a == NULL)
        panic("out of swap space");
    p->p_flag |= SLOCK;
    xccdec(p->p_textp);
    if (p->p_addr == uhome)
        uflush(uhome); // ... or the image goes to disk with a stale u page
    swap(a, p->p_addr, os, B_WRITE);
    nswapout++;
    //
    // A SWAP SLOT MUST BE WRITTEN IN FULL BEFORE IT IS READ, and this is the one path where
    // it would not be.  expand() (kernel/slp.c) raises p_size to the NEW size and then calls
    // us with the OLD one in os, because the old size is all the core there is; swapin()
    // then reads p_size words back.  On the PDP-11 the tail came back as whatever the disk
    // happened to hold, and v7 did not care -- getxfile() clearsegs the new image and grow()
    // copies the stack into it, so nothing ever reads those words.
    //
    // On this machine a block that has never been written is not garbage, it is an I/O
    // ERROR: the drum container grows only as far as the highest zone ever written, a read
    // past that fails (SIMH's besm6_drum.c fails the short fread), dev/mb.c's EXT_IOERR poll
    // cannot tell that from a missing drum, and swap() panics with "IO err in swap".  So the
    // first process ever to grow under memory pressure killed the machine.
    //
    // Zero rather than don't-care because it costs nothing here and is assertable:
    // kernel/test/uswap's first leg is this exact call and checks the tail comes back zero.
    // Only expand() passes os < p_size and it always passes ff, so the page we clear is one
    // the mfree() below is about to take back; the `ff' test keeps a future !ff caller from
    // having its live image zeroed, at the price of an unspecified (but readable) tail.
    //
    // `n', not `os': the mfree() below frees os words of CORE, and there are only os of them.
    for (n = os; n < p->p_size; n += PGSZ) {
        if (ff)
            clearseg(p->p_addr);
        swap(a + wtodb(n), p->p_addr, PGSZ, B_WRITE);
    }
    if (ff) {
        mfree(coremap, os, p->p_addr);
        if (p->p_addr == uhome)
            uhome = NOUHOME; // the home we just flushed to no longer exists
    }
    p->p_addr = a;
    p->p_flag &= ~(SLOAD | SLOCK);
    p->p_time = 0;
    if (runout) {
        runout = 0;
        wakeup((chan_t)&runout);
    }
}

// relinquish use of the shared text segment
// of a process.
void xfree()
{
    register struct text *xp;
    register struct inode *ip;

    if ((xp = u.u_procp->p_textp) == NULL)
        return;
    xlock(xp);
    xp->x_flag &= ~XLOCK;
    u.u_procp->p_textp = NULL;
    ip                 = xp->x_iptr;
    if (--xp->x_count == 0 && (ip->i_mode & ISVTX) == 0) {
        xp->x_iptr = NULL;
        mfree(swapmap, wtodb(xp->x_size), xp->x_daddr);
        mfree(coremap, xp->x_size, xp->x_caddr);
        ip->i_flag &= ~ITEXT;
        if (ip->i_flag & ILOCK)
            ip->i_count--;
        else
            iput(ip);
    } else
        xccdec(xp);
}

// Attach to a shared text segment.
// If there is no shared text, just return.
// If there is, hook up to it:
// if it is not currently being used, it has to be read
// in from the inode (ip); the written bit is set to force it
// to be written out as appropriate.
// If it is being used, but is not currently in core,
// a swap has to be done to get it back.
void xalloc(register struct inode *ip)
{
    register struct text *xp;
    register int ts;
    register struct text *xp1;

    if (u.u_exdata.ux_tsize == 0)
        return;
    xp1 = NULL;
    for (xp = &text[0]; xp < &text[NTEXT]; xp++) {
        if (xp->x_iptr == NULL) {
            if (xp1 == NULL)
                xp1 = xp;
            continue;
        }
        if (xp->x_iptr == ip) {
            xlock(xp);
            ntextjoin++; // a second process on one binary's text: what task 26 is about
            xp->x_count++;
            u.u_procp->p_textp = xp;
            if (xp->x_ccount == 0)
                xexpand(xp);
            else
                xp->x_ccount++;
            xunlock(xp);
            return;
        }
    }
    if ((xp = xp1) == NULL) {
        printf("out of text");
        psignal(u.u_procp, SIGKIL);
        return;
    }
    xp->x_flag   = XLOAD | XLOCK;
    xp->x_count  = 1;
    xp->x_ccount = 0;
    xp->x_iptr   = ip;
    ip->i_flag |= ITEXT;
    ip->i_count++;
    // The shared read-only region is the header hole + const + text (cross/besm6/
    // b.out.h); it is read into word BADDR, so ts carries the BADDR-word hole and
    // the const image lands where its file offset (HDRSZ) equals its word address.
    // Words 0..BADDR-1 of the page are left unread -- harmless: no program names
    // them and virtual word 0 is the black hole regardless.
    ts         = pground(BADDR + btow(u.u_exdata.ux_csize + u.u_exdata.ux_tsize));
    xp->x_size = ts;
    if ((xp->x_daddr = malloc(swapmap, (int)wtodb(ts))) == NULL)
        panic("out of swap space");
    u.u_procp->p_textp = xp;
    xexpand(xp);
    estabur(ts, 0, 0, 0, RW);
    u.u_count  = u.u_exdata.ux_csize + u.u_exdata.ux_tsize;
    u.u_offset = sizeof(u.u_exdata);
    u.u_base   = (caddr_t)(int *)BADDR;
    u.u_segflg = 2;
    u.u_procp->p_flag |= SLOCK;
    readi(ip);
    u.u_procp->p_flag &= ~SLOCK;
    u.u_segflg = 0;
    xp->x_flag = XWRIT;
}

// Assure core for text segment
// Text must be locked to keep someone else from
// freeing it in the meantime.
// x_ccount must be 0.
void xexpand(register struct text *xp)
{
    if ((xp->x_caddr = malloc(coremap, xp->x_size)) != NULL) {
        if ((xp->x_flag & XLOAD) == 0) {
            swap(xp->x_daddr, xp->x_caddr, xp->x_size, B_READ);
            ntextin++;
        }
        xp->x_ccount++;
        xunlock(xp);
        return;
    }
    if (save(u.u_ssav)) {
        sureg();
        return;
    }
    xswap(u.u_procp, 1, 0);
    xunlock(xp);
    u.u_procp->p_flag |= SSWAP;
    qswtch();
    // no return
}

// Lock and unlock a text segment from swapping
void xlock(register struct text *xp)
{
    while (xp->x_flag & XLOCK) {
        xp->x_flag |= XWANT;
        sleep((chan_t)xp, PSWP);
    }
    xp->x_flag |= XLOCK;
}

void xunlock(register struct text *xp)
{
    if (xp->x_flag & XWANT)
        wakeup((chan_t)xp);
    xp->x_flag &= ~(XLOCK | XWANT);
}

// Decrement the in-core usage count of a shared text segment.
// When it drops to zero, free the core space.
void xccdec(register struct text *xp)
{
    if (xp == NULL || xp->x_ccount == 0)
        return;
    xlock(xp);
    if (--xp->x_ccount == 0) {
        if (xp->x_flag & XWRIT) {
            xp->x_flag &= ~XWRIT;
            swap(xp->x_daddr, xp->x_caddr, xp->x_size, B_WRITE);
            ntextout++;
        }
        mfree(coremap, xp->x_size, xp->x_caddr);
    }
    xunlock(xp);
}

// free the swap image of all unused saved-text text segments
// which are from device dev (used by umount system call).
void xumount(register int dev)
{
    register struct text *xp;

    for (xp = &text[0]; xp < &text[NTEXT]; xp++)
        if (xp->x_iptr != NULL && dev == xp->x_iptr->i_dev)
            xuntext(xp);
}

// remove a shared text segment from the text table, if possible.
void xrele(register struct inode *ip)
{
    register struct text *xp;

    if ((ip->i_flag & ITEXT) == 0)
        return;
    for (xp = &text[0]; xp < &text[NTEXT]; xp++)
        if (ip == xp->x_iptr)
            xuntext(xp);
}

// remove text image from the text table.
// the use count must be zero.
void xuntext(register struct text *xp)
{
    register struct inode *ip;

    xlock(xp);
    if (xp->x_count) {
        xunlock(xp);
        return;
    }
    ip = xp->x_iptr;
    xp->x_flag &= ~XLOCK;
    xp->x_iptr = NULL;
    mfree(swapmap, wtodb(xp->x_size), xp->x_daddr);
    ip->i_flag &= ~ITEXT;
    if (ip->i_flag & ILOCK)
        ip->i_count--;
    else
        iput(ip);
}
