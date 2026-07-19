/* V7/x86 source code: see www.nordier.com/v7x86 for details. */
/* Copyright (c) 2007 Robert Nordier.  All rights reserved. */

/*
 * BESM-6 magnetic disk driver (МД / КМД) -- SKELETON, tasks 18b.4 and 18b.5.
 *
 * The disks carry the filesystems: this is major 0, the device rootdev and pipedev name.
 * SIMH models 8 controllers of 8 units; this kernel reaches controllers 3 and 4, the two
 * the 033 address map has room for.  Swap lives on the drums instead (dev/mb.c) so that
 * paging and filesystem traffic do not queue behind one another -- the two are separate
 * channels and can transfer at the same time.
 *
 * THIS IS THE ONLY DEVICE IN THE MACHINE WITH A TWO-STEP PROTOCOL, and it is the reason
 * the disk is harder than the drum:
 *
 *   1. 033 3 (or 033 4) hands the controller an exchange control word saying what to
 *      move and where in memory.  Nothing happens yet.  This also lowers the controller's
 *      ГРП free bit and clears its error flag.
 *   2. 033 023 (or 033 024) issues controller commands -- select a group, select a unit,
 *      then supply the track address.  SUPPLYING THE TRACK ADDRESS IS WHAT TRANSFERS.
 *      The unit select is a one-hot mask in inverted bit order: bit 8 is unit 0.
 *
 * Completion arrives as GRP_CHAN3_FREE or GRP_CHAN4_FREE -- wired bits; see the note in
 * sys/besm6dev.h.  Bracket one exchange with mgrpon()/mgrpoff() (task 18b.2): arm after
 * step 1, which is what lowers the free bit, and disarm in mdintr() before iodone().
 * Arming after step 2 races the transfer.  A status register at 033 4003 / 033 4004 reports
 * errors, which the drum has no equivalent of (task 18b.5).
 *
 * What is here now is the driver's public surface only -- the entry points conf.c wires
 * into bdevsw/cdevsw -- so that the kernel builds and links while the real 033-channel
 * driver is written.  Read doc/Besm6_Peripherals.md, "Magnetic disks", first: it has the
 * command encodings and the note that SIMH logs the seek and plain read/write commands
 * without acting on them.
 */

// clang-format off
#include "sys/types.h"
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/buf.h"
#include "sys/besm6dev.h"
#include "sys/besm6disk.h"
// clang-format on

struct buf rmdbuf;
struct buf mdtab;

void mdopen(dev_t dev, int rw)
{
    /* TODO 18b.4: unit and partition setup, once minor(dev) means something. */
}

void mdstrategy(register struct buf *bp)
{
    /*
     * TODO 18b.4: assemble the control word from bufpaddr(bp) and b_blkno, queue on
     * mdtab, then run the two-step sequence above.  Until that exists, fail every
     * request rather than pretend the transfer happened.
     */
    bp->b_flags |= B_ERROR;
    iodone(bp);
}

void mdintr()
{
    /* TODO 18b.4: exchange complete (GRP_CHAN3_FREE / GRP_CHAN4_FREE). */
}

void mdread(dev_t dev)
{
    physio(mdstrategy, &rmdbuf, dev, B_READ);
}

void mdwrite(dev_t dev)
{
    physio(mdstrategy, &rmdbuf, dev, B_WRITE);
}
