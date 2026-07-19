/* V7/x86 source code: see www.nordier.com/v7x86 for details. */
/* Copyright (c) 2007 Robert Nordier.  All rights reserved. */

/*
 * BESM-6 magnetic drum driver (МБ) -- SKELETON, task 18b.3.
 *
 * The drums are the machine's paging store, and this kernel's swap device: two units of
 * 256 zones each, a zone being 8 service words plus 1 Kword of data.  That is 512 blocks
 * of BSIZEW per drum -- a swap area, not somewhere a filesystem could live, which is why
 * the drums are a major of their own with the disks (dev/md.c) on major 0.
 *
 * A transfer is ONE INSTRUCTION.  The control word names the memory page, the direction
 * and the zone all at once, and issuing it to 033 1 or 033 2 starts the exchange; there
 * is no command sequence, unlike the disk.  Completion arrives as GRP_DRUM1_FREE or
 * GRP_DRUM2_FREE in ГРП -- wired bits, so they cannot be dismissed with MOD_GRPCLR and
 * must not sit armed in МГРП while the drum is idle (see sys/besm6dev.h, and task 18b.2,
 * which owns that mechanism and is a prerequisite of filling this file in).
 *
 * What is here now is the driver's public surface only -- the entry points conf.c wires
 * into bdevsw/cdevsw -- so that the kernel builds and links while the real 033-channel
 * driver is written.  doc/Besm6_Peripherals.md has the control word field by field;
 * doc/Intrinsics.md §6.3 is a drum page read already written out in C.
 */

// clang-format off
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/buf.h"
#include "sys/besm6dev.h"
#include "sys/besm6disk.h"
// clang-format on

struct buf rmbbuf;
struct buf mbtab;

void mbopen(dev_t dev, int rw)
{
    /* TODO 18b.3: unit setup. */
}

void mbstrategy(register struct buf *bp)
{
    /*
     * TODO 18b.3: assemble the control word from bufpaddr(bp) and b_blkno, queue on
     * mbtab, and issue it with __besm6_ext(EXT_DRUM1, cw).  Until that exists, fail
     * every request rather than pretend the transfer happened.
     */
    bp->b_flags |= B_ERROR;
    iodone(bp);
}

void mbintr()
{
    /* TODO 18b.3: exchange complete (GRP_DRUM1_FREE / GRP_DRUM2_FREE). */
}

void mbread(dev_t dev)
{
    physio(mbstrategy, &rmbbuf, dev, B_READ);
}

void mbwrite(dev_t dev)
{
    physio(mbstrategy, &rmbbuf, dev, B_WRITE);
}
