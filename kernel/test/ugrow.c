/*
 * ugrow -- stack growth on the real machine (task 17).
 *
 * utrap proved that a data-protection fault from user mode frames the machine and that the
 * faulting instruction re-executes.  This test asks what the handler should DO about one
 * particular fault: a store just past the top of the user stack.
 *
 * The claim task 17 rests on is a geometry claim.  The BESM-6 user stack lives at virtual page
 * USTKPAGE (28 = 070000) and grows UP, and sureg() (kernel/utab.c) lays its physical pages out
 * as the TAIL of the process image.  So growing the stack by one page appends that page at the
 * next higher virtual address and at the end of the image AT THE SAME TIME -- which means every
 * page already in the stack keeps exactly the address it had, and grow() has nothing to move.
 * That is why the new grow() (kernel/sig.c) has no copyseg shuffle where the x86 original did.
 *
 * If that ever stops being true, this test fails: it seeds the ONE stack page the process starts
 * with, grows the stack out from under it, and checks that the page is still there, still at the
 * same physical address, still holding its sentinel -- while the store that faulted lands in the
 * page that has just come into existence.
 *
 *   virtual page   0    |  1   | 2 .. 27 | 28  | 29     | 30 31
 *                  text | data | closed  | stk | GROWN  | closed
 *
 * grow() itself cannot be linked here: it calls expand(), which drags in the whole swapper.  So
 * trap() below performs grow()'s MAPPING half -- bump u_ssize, clear the new tail, sureg() --
 * exactly as utrap.c stands in for grow() with a bare `u.u_dsize += PGSZ'.  What is under test
 * is the geometry, and the geometry is entirely in sureg().
 *
 * ugrow.ini asserts ACC == 0.  A nonzero ACC names the failing check -- see the F_* bits.
 */

// clang-format off
#include "sys/types.h"
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/dir.h"
#include "sys/user.h"
#include "sys/proc.h"
#include "sys/text.h"
#include "sys/reg.h"
#include "sys/seg.h"
#include "sys/besm6dev.h"
// clang-format on

#include <besm6.h>

/*
 * The kernel globals utab.o refers to.  In the kernel `u' is absolute at 076000 and maxmem is
 * counted at boot; here they are ordinary storage (as in mmutest, uintr and utrap).
 */
struct user u;
int maxmem = 512 * 1024;

static struct proc pr;
static struct text tx;

/* crt0g.S */
extern unsigned uprogadr; /* uprog's link-time word address, as a plain integer */
extern int *ustkbase;     /* base of the gate's stack: where the trap frame lands */
void gouser(unsigned uentry);
void halt(unsigned mask);

/* brz.s */
void drainbrz(void);

/* Must match the EQUs in crt0g.S. */
#define USPV    070000U /* forged r15: the base of the user stack */
#define GROWADR 072000U /* virtual page 29: one word past the stack top */
#define DONEADR 074000U /* virtual page 30: never opened */
#define DATADR  02000U  /* virtual page 1: the data page */

#define IMAGEPG 16 /* physical page of the process image (data + stack), free memory */

/* The sentinels. */
#define SENTNEW 0246246U /* seeded in the data page; uprog stores it into the grown page */
#define SENTOLD 0525252U /* seeded in the stack page that already exists */

/* Fault-mask bits, reported in the accumulator by halt().  Zero means every check passed. */
#define F_PAGE1   0001 /* fault 1 was not a data violation on virtual page USTKPAGE + 1 */
#define F_PAGE2   0002 /* fault 2 was not a data violation on virtual page USTKPAGE + 2 */
#define F_R15     0004 /* the framed r15 is not the user's */
#define F_GROWN   0010 /* the retried store did not land in the newly grown page */
#define F_OLDSENT 0020 /* THE POINT: the pre-existing stack page lost its sentinel */
#define F_OLDMAP  0040 /* THE POINT: the pre-existing stack page changed physical address */
#define F_NFAULT  0100 /* the wrong number of faults arrived */
#define F_SSIZE   0200 /* u_ssize does not describe the two pages that are now mapped */

static unsigned mask;     /* accumulated failures */
static unsigned nfault;   /* which fault we are in: 1, 2 */
static unsigned oldstkph; /* physical address of the stack page, sampled BEFORE the growth */

/* Physical base of the data region == the physical address of virtual page 1. */
#define DBASE (IMAGEPG * PGSZ + USIZE)
/* ... the stack page that exists at entry follows it ... */
#define STKBASE (DBASE + PGSZ)
/* ... and the page the growth hands out follows that. */
#define NEWBASE (STKBASE + PGSZ)

/*
 * The fault handler, reached from crt0g.S's trapgate -- the same C signature the real kernel's
 * trap() has.  See utrap.c for why the frame is found at `ustkbase' here rather than at
 * u.u_stack: `u' above is ordinary storage, not the fixed page at 076000.
 */
void trap(void)
{
    struct trap *tr = (struct trap *)ustkbase;
    unsigned *f     = (unsigned *)tr; /* the frame, aliased in place */
    unsigned grp, page;

    nfault++;

    /*
     * The restart protocol -- a copy of kernel/trap.c's.  KEEP THE TWO IN STEP: this test
     * cannot link the kernel's trap.c (which pulls in the whole kernel), so the four lines are
     * duplicated here, exactly as the gate itself is duplicated in crt0g.S.
     */
    if (tr->spsw & SPSW_NEXT_RK) {
        tr->ret--; /* the saved PC is the faulting WORD plus one */
        tr->spsw &= ~SPSW_NEXT_RK;
    }

    if (f[R15] != USPV)
        mask |= F_R15;

    grp  = __besm6_mod(MOD_GRP, 0);
    page = (grp & GRP_PAGE_MASK) >> GRP_PAGE_SHIFT;
    __besm6_mod(MOD_GRPCLR, ~GRP_OPRND_PROT);

    if (nfault == 1) {
        /*
         * The store one word past the stack top.  This is the fault the real kernel turns into
         * a stack growth: T_DATA + USER -> grow(page), with the page as reported.
         */
        if (!(grp & GRP_OPRND_PROT) || page != USTKPAGE + 1)
            mask |= F_PAGE1;

        /* Where the existing stack page lives, sampled BEFORE anything moves. */
        oldstkph = physaddr(USPV);

        /*
         * grow()'s mapping half.  The new page is the tail of the image -- which, the image
         * being laid out u-area, data, stack, is the word after the stack pages already there.
         * The real grow() gets it from expand(); here p_size was sized for it up front.
         */
        clearseg(NEWBASE);
        u.u_ssize += PGSZ;
        sureg();

        /* Nothing else to do: the gate's epilogue re-executes the store. */
        return;
    }

    /* Fault 2: uprog's "done" signal, on a page that was never opened. */
    if (!(grp & GRP_OPRND_PROT) || page != USTKPAGE + 2)
        mask |= F_PAGE2;
    if (nfault != 2)
        mask |= F_NFAULT;

    /*
     * uprog's stores were MAPPED; these reads are physical, so the write cache has to be
     * settled first or they would miss the dirty line (mmutest.c makes the same point).
     */
    drainbrz();

    /* The retried store reached the page the growth handed out. */
    if (*(volatile unsigned *)NEWBASE != SENTNEW)
        mask |= F_GROWN;

    /*
     * THE POINT OF THIS TEST.  The stack page that already existed must not have moved: same
     * physical page, same contents, same virtual address.  An implementation that shuffled the
     * stack the way the x86 grow() did would have slid this page up by one and both of these
     * would fail.
     */
    if (*(volatile unsigned *)STKBASE != SENTOLD)
        mask |= F_OLDSENT;
    if (physaddr(USPV) != oldstkph || oldstkph != STKBASE)
        mask |= F_OLDMAP;

    /* And the growth is described by u_ssize: two pages, 28 and 29, mapped. */
    if (u.u_ssize != 2 * PGSZ || physaddr(GROWADR) != NEWBASE)
        mask |= F_SSIZE;

    halt(mask); /* never returns */
}

int main()
{
    unsigned uaddr, uentry;

    /*
     * Build the user map: uprog's OWN physical page at virtual page 0 (so the forged user runs
     * the real code), one page of text, one of data, and a ONE-page stack at USTKPAGE.  That
     * leaves virtual page 29 closed -- the page the stack grows into -- and page 30 closed for
     * good, which is uprog's way of saying it is done.
     */
    uaddr  = uprogadr;           /* uprog's WORD address (a plain integer from the linker) */
    uentry = uaddr & (PGSZ - 1); /* its offset within virtual page 0 */

    tx.x_caddr = uaddr & ~(PGSZ - 1); /* map virtual page 0 -> uprog's physical page */
    tx.x_size  = PGSZ;

    pr.p_addr = IMAGEPG * PGSZ;
    /*
     * The image is sized for the page the growth will hand out.  In the kernel expand() does
     * this; here it is up front, because expand() cannot be linked (it is the swapper).
     */
    pr.p_size  = USIZE + PGSZ + 2 * PGSZ;
    pr.p_textp = &tx;

    u.u_procp = &pr;
    u.u_tsize = PGSZ;
    u.u_dsize = PGSZ;
    u.u_ssize = PGSZ;

    sureg();

    /*
     * Seed the two sentinels physically -- the kernel runs unmapped, so a kernel address IS a
     * physical address -- and settle the write cache so the user's mapped reads see them.
     * SENTNEW is what uprog picks up out of its data page and plants past the stack top;
     * SENTOLD is what must still be in the stack page after the growth.
     */
    *(volatile unsigned *)DBASE   = SENTNEW;
    *(volatile unsigned *)STKBASE = SENTOLD;
    drainbrz();

    /*
     * Mask every interrupt source: the forged СПСВ enters user mode with БлПр CLEAR, and the
     * interval timer re-arms GRP_TIMER at reset.  Nothing here wants an external interrupt.
     */
    __besm6_mod(MOD_MGRP, 0);

    gouser(uentry); /* forge the user context and enter it; never returns */
    return 0;
}
