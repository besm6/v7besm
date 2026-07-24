// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// Character lists -- the queues the tty layer holds typed and printing characters in.
//
// A clist is a linked chain of blocks drawn from one shared pool, `cfree[NCLIST]'.  The
// head names the block and the slot the next character comes OUT of, the tail the block
// and the slot the next one goes IN to; a block is returned to `cfreelist' the moment its
// last character leaves.  That much is v7's design and is unchanged.
//
// WHAT IS NOT V7 IS HOW A CURSOR IS SPELLED, and it had to change.  v7 kept two `char *'
// cursors, `c_cf' and `c_cl', and derived everything else from their integer values:
//
//      bp = (struct cblock *)((int)bp & ~CROUND);   // which block is this cursor in?
//      if (((int)p->c_cf & CROUND) == 0)            // did it just leave that block?
//
// Both work on a byte-addressed machine where a cblock is power-of-two sized and
// aligned, so the low bits of a character's address ARE its offset within the block.
// Here they are neither correct nor nearly correct.  A `char *' on the BESM-6 is a FAT
// POINTER -- marker in bit 48, byte offset in bits 47-45, word address in bits 15-1
// (doc/Besm6_Data_Representation.md, "Fat pointer") -- so `(int)cp & 037' masks five
// bits of the WORD address and says nothing whatever about which of the word's six
// characters the cursor names.  The boundary test would fire once every 32 words rather
// than once per block, and the block-base recovery would hand the free list a pointer
// into the middle of some other block.  Walking a cursor (`c_cf++') was always fine --
// that is the compiler's own b$pinc -- it is only arithmetic ON the pointer value that
// cannot survive.
//
// So a cursor here is a block pointer plus an int index (sys/tty.h), the boundary test is
// `ix == CBSIZE', and CROUND is gone from sys/param.h.  Same trap as the b_un.b_addr fat
// pointer fstest found: a fat pointer stored where a word address is read back is silent,
// never fatal, so prefer the spelling that makes the bad one impossible.
//
// ndqb()/ndflush() -- v7's "how many characters lie contiguously from here" pair -- are
// gone with the pointer arithmetic that was their only reason to exist.  They serve a
// driver that hands a run of a block straight to DMA hardware; the Consul takes one
// character per `ext' (kernel/dev/sc.c) and nothing in this kernel called them.  getw()/
// putw() on a clist went the same way: unused, and the names are stdio's.

// clang-format off
#include "sys/types.h"
#include "sys/param.h"
#include "sys/tty.h"
#include "sys/systm.h"
#include "sys/conf.h"
// clang-format on

struct cblock {
    struct cblock *c_next;
    char c_info[CBSIZE];
};

struct cblock cfree[NCLIST];
struct cblock *cfreelist;
int nchrdev;

// Take one character off the front of a clist.  -1 if it is empty.
int getc(register struct clist *p)
{
    register struct cblock *bp;
    register int c, s;

    s = spl6();
    if (p->c_cc <= 0) {
        p->c_cc = 0;
        p->c_hd = p->c_tl = NULL;
        splx(s);
        return (-1);
    }
    bp = p->c_hd;
    c  = bp->c_info[p->c_hix++] & 0377;
    if (--p->c_cc == 0) {
        // The queue just emptied: the head block is also the tail, and both
        // cursors start again from nothing.
        p->c_hd = p->c_tl = NULL;
        bp->c_next        = cfreelist;
        cfreelist         = bp;
    } else if (p->c_hix == CBSIZE) {
        // The head block is spent but characters remain, so the chain has a
        // next block -- putc() never advances the tail without linking one.
        p->c_hd    = bp->c_next;
        p->c_hix   = 0;
        bp->c_next = cfreelist;
        cfreelist  = bp;
    }
    splx(s);
    return (c);
}

// Put one character on the end of a clist.  -1 if the block pool is exhausted,
// which is the caller's cue to drop the character (ttyinput) or to sleep (ttwrite).
int putc(int c, register struct clist *p)
{
    register struct cblock *bp;
    register int s;

    s = spl6();
    if (p->c_tl == NULL || p->c_tix == CBSIZE) {
        if ((bp = cfreelist) == NULL) {
            splx(s);
            return (-1);
        }
        cfreelist  = bp->c_next;
        bp->c_next = NULL;
        if (p->c_tl == NULL) {
            p->c_hd  = bp;
            p->c_hix = 0;
            p->c_cc  = 0;
        } else {
            p->c_tl->c_next = bp;
        }
        p->c_tl  = bp;
        p->c_tix = 0;
    }
    p->c_tl->c_info[p->c_tix++] = c;
    p->c_cc++;
    splx(s);
    return (0);
}

// Copy a clist into a buffer; return the number of characters moved.
int q_to_b(register struct clist *q, register char *cp, register int cc)
{
    register int c, n;

    for (n = 0; n < cc; n++) {
        if ((c = getc(q)) < 0)
            break;
        *cp++ = c;
    }
    return (n);
}

// Copy a buffer into a clist; return the number of characters NOT transferred,
// which is nonzero only when the block pool ran dry.
int b_to_q(register char *cp, register int cc, struct clist *q)
{
    if (cc <= 0)
        return (0);
    while (cc > 0) {
        if (putc(*cp++, q) < 0)
            break;
        cc--;
    }
    return (cc);
}

// Initialize clists by freeing all character blocks, then count the character
// devices. (Once-only routine)
void cinit()
{
    register struct cblock *cp;
    register struct cdevsw *cdp;
    register int n;

    for (cp = cfree; cp < &cfree[NCLIST]; cp++) {
        cp->c_next = cfreelist;
        cfreelist  = cp;
    }
    n = 0;
    for (cdp = cdevsw; cdp->d_open; cdp++)
        n++;
    nchrdev = n;
}
