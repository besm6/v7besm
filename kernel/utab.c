/* V7/x86 source code: see www.nordier.com/v7x86 for details. */
/* Copyright (c) 1999 Robert Nordier.  All rights reserved. */

/*
 * Per-process address-space setup.
 *
 * The whole mapping is eight write-only page registers РП (002 020-027) and the
 * protection register РЗ (002 030-033), over 32 virtual pages of 1 Kword.  Neither
 * can be read back, so u.u_upt[8] is the only copy of the mapping there is: the
 * hardware registers are a write-only cache of it, reloaded in twelve `mod's.
 *
 * The two packings are complementary by design.  РП uses accumulator bits 1-20 and
 * 29-48; РЗ uses bits 21-28.  So one word carries a quartet of page numbers (virtual
 * pages 4i..4i+3) and -- in the even words -- the protection byte of eight pages
 * (8j..8j+7), and neither write needs a shift.
 *
 * Read doc/Memory_Mapping.md, "Programming the MMU", before touching any of this.
 */

// clang-format off
#include <besm6.h>
#include "sys/types.h"
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/dir.h"
#include "sys/user.h"
#include "sys/proc.h"
#include "sys/text.h"
#include "sys/seg.h"
// clang-format on

/*
 * The accumulator bits page k of a quartet contributes to, most significant first:
 * its bit 10 at acc 45+k, bit 9 at 41+k, bit 8 at 37+k, bit 7 at 33+k, bit 6 at
 * 29+k, and its low five bits together at acc 5k+1..5k+5.  (Bit 1 is the LSB, so
 * acc bit N is 1 << (N-1).)
 *
 * That is a bit-scatter, and `aux' is a bit-scatter instruction: it takes the source
 * from bit 48 downward and deposits into the mask's set bits, also from bit 48
 * downward.  So a page number left-aligned to bit 48 -- shifted left by 48-10 --
 * scatters into exactly these positions, and `apx', its inverse, gathers it back.
 */
#define PGBITS 10 /* width of a descriptor in the word (masked to 9 by the hardware) */
#define RPBITS(k)                                                                        \
    ((1U << (44 + (k))) | (1U << (40 + (k))) | (1U << (36 + (k))) | (1U << (32 + (k))) | \
     (1U << (28 + (k))) | (037U << (5 * (k))))

static const unsigned rpmask[4] = { RPBITS(0), RPBITS(1), RPBITS(2), RPBITS(3) };

/*
 * Drain the БРЗ write cache before reloading РП -- nine consecutive stores to
 * physical 1-7, which is why it is in assembly (brz.s) and not here.
 */
void drainbrz(void);

/*
 * The physical page mapped at virtual page v, or 0 if the page is not mapped.
 * Zero is the hardware's own "no page": a zero РП descriptor is what makes a page
 * non-executable, and physical page 0 is therefore never handed to a process.
 */
static unsigned uptget(int v)
{
    return __besm6_apx(u.u_upt[v >> 2], rpmask[v & 3]) >> (48 - PGBITS);
}

/*
 * Rebuild the shadow map from the process's sizes and load it into РП and РЗ.
 *
 * The image at p_addr is the u-area page, then data, then stack; the text is
 * separate when it is shared.  The u page is NOT in the map -- it lives at a fixed
 * physical address and the kernel reaches it unmapped.
 *
 * Writing РП here is safe precisely because the kernel runs unmapped: changing the
 * map changes nothing about how the kernel addresses its own memory.
 */
void sureg()
{
    unsigned pg[NPAGE]; /* physical page per virtual page; 0 = not mapped */
    unsigned prot;      /* one bit per virtual page; set = closed to data */
    unsigned a, w;
    register int i, k, v;
    struct text *tp;
    unsigned taddr, daddr;

    for (i = 0; i < NPAGE; i++)
        pg[i] = 0;
    prot = ~0U; /* every page closed until we map it */

    taddr = daddr = u.u_procp->p_addr;
    if ((tp = u.u_procp->p_textp) != NULL)
        taddr = tp->x_caddr;

    /*
     * Text from virtual page 0 up, then data.  Text is left OPEN to data access:
     * this machine has no read-only page -- РЗ closes a page to reads as well as
     * writes -- and a closed text page would take the program's own constant pool
     * with it.
     */
    v = 0;
    for (a = taddr; a < taddr + u.u_tsize; a += PGSZ) {
        pg[v] = a >> PGSH;
        prot &= ~(1U << v);
        v++;
    }
    a = daddr + USIZE;
    for (; a < daddr + USIZE + u.u_dsize; a += PGSZ) {
        pg[v] = a >> PGSH;
        prot &= ~(1U << v);
        v++;
    }

    /* The stack sits at the top of the image and at virtual page USTKPAGE, growing up. */
    v = USTKPAGE;
    for (; a < daddr + USIZE + u.u_dsize + u.u_ssize; a += PGSZ) {
        pg[v] = a >> PGSH;
        prot &= ~(1U << v);
        v++;
    }

    for (i = 0; i < 8; i++) {
        w = 0;
        for (k = 0; k < 4; k++)
            w |= __besm6_aux(pg[4 * i + k] << (48 - PGBITS), rpmask[k]);
        if ((i & 1) == 0) {
            /* even word: it also carries the РЗ byte of pages 8j..8j+7, j = i/2 */
            w |= ((prot >> (4 * i)) & 0377) << 20;
        }
        u.u_upt[i] = w;
    }

    drainbrz();
    for (i = 0; i < 8; i++)
        __besm6_mod(020 + i, u.u_upt[i]);
    for (i = 0; i < 4; i++)
        __besm6_mod(030 + i, u.u_upt[2 * i]);
}

/*
 * Sizes are in words, page-aligned: the image must fit the 32 pages the user
 * gets, text+data must stay below the stack base, and the stack must fit the
 * pages above it.
 *
 * xrw (RO/RW from seg.h) is accepted and ignored: the machine has no read-only
 * page.  So is sep -- there is no I/D separation here either.
 */
int estabur(int nt, int nd, int ns, int sep, int xrw)
{
    if (nt + nd + ns > MAXMEM)
        goto err;
    if (nt + nd > USTKPAGE * PGSZ)
        goto err;
    if (ns > (NPAGE - USTKPAGE) * PGSZ)
        goto err;
    if (nt + nd + ns + USIZE > maxmem)
        goto err;
    u.u_tsize = nt;
    u.u_dsize = nd;
    u.u_ssize = ns;
    sureg();
    return (0);
err:
    u.u_error = ENOMEM;
    return (-1);
}

/*
 * copyseg()/clearseg() -- one page, copied or zeroed -- live in kernel/seg.S.
 * They reach a page above 0100000, which no caddr_t (a 15-bit word field) can
 * name, through a mapped window; see the bracket there.
 */

/*
 * The physical word address a virtual one maps to, or 0 if it is not mapped.
 */
unsigned physaddr(unsigned addr)
{
    unsigned pgno;

    pgno = uptget(addr >> PGSH);
    if (pgno == 0)
        return (0);
    return (pgno << PGSH) | (addr & (PGSZ - 1));
}

/*
 * Is the user's [addr, addr+count) -- in WORDS -- entirely mapped?  A zero
 * descriptor means the page is not there, and the answer is EFAULT.
 *
 * This is what lets the assembly copies assume a valid address, which is why v7's
 * whole nofault machinery disappears.  rw is accepted and ignored: a page is open
 * to data or closed to it, with no read/write distinction to check.
 */
int useracc(unsigned addr, unsigned count, int rw)
{
    unsigned v, last;

    if (count == 0)
        return (1);
    if (addr + count > NPAGE * PGSZ || addr + count < addr)
        return (0);
    last = (addr + count - 1) >> PGSH;
    for (v = addr >> PGSH; v <= last; v++)
        if (uptget((int)v) == 0)
            return (0);
    return (1);
}

/*
 * The physical word address of the user's [addr, addr+count) -- in WORDS -- but only
 * if the whole range is mapped AND lands on ONE CONTIGUOUS run of physical pages.
 * Returns 0 otherwise; physical page 0 is never handed to a process, so 0 is a safe
 * "no" (physaddr() uses it the same way).
 *
 * A device transfer is described by one physical address plus a length: the drum/disk
 * control word carries a 9-bit page number and the driver derives page n by adding
 * n * PGSZ (doc/Besm6_Peripherals.md).  So a request whose pages are scattered cannot
 * be expressed at all, and must be refused rather than half-issued.
 *
 * Today the check cannot fire.  malloc() is first-fit and returns ONE contiguous run,
 * expand() keeps the image one run, and sureg() maps text, data and stack sequentially
 * within it -- so every range that survives physio()'s text and data/stack-gap tests
 * is contiguous by construction.  This asserts that invariant rather than coding around
 * a case the allocator forbids: if a future swapper ever breaks it, this is what says
 * so, instead of a driver quietly DMAing into a page that belongs to someone else.
 *
 * Lives here, not in bio.c, because uptget() is static to this file and the buffer
 * cache has no business knowing how a descriptor is packed.
 */
unsigned physrange(unsigned addr, unsigned count)
{
    unsigned v, last, first;

    if (count == 0 || !useracc(addr, count, 0))
        return (0);
    first = addr >> PGSH;
    last  = (addr + count - 1) >> PGSH;
    for (v = first + 1; v <= last; v++)
        if (uptget((int)v) != uptget((int)first) + (v - first))
            return (0);
    return (physaddr(addr));
}
