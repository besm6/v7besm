/* V7/x86 source code: see www.nordier.com/v7x86 for details. */
/* Copyright (c) 2007 Robert Nordier.  All rights reserved. */

/*
 * Memory disk driver
 */

// clang-format off
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/buf.h"
#include "sys/dir.h"
#include "sys/user.h"
// clang-format on

char *mdmem = (char *)(PHY + 0xe00000); /* memory to use */
int mdsz = 4096;                        /* size in blocks */

struct buf mdtab;
struct buf rmdbuf;

mdstrategy(bp) register struct buf *bp;
{
    unsigned sz, x;

    sz = (bp->b_bcount + BMASK) >> BSHIFT;
    if (bp->b_blkno + sz > mdsz) {
        bp->b_flags |= B_ERROR;
        iodone(bp);
        return;
    }
    bp->av_forw = NULL;
    x = bp->b_blkno << BSHIFT;
    if (bp->b_flags & B_READ)
        bcopy(mdmem + x, bp->b_un.b_addr, bp->b_bcount);
    else
        bcopy(bp->b_un.b_addr, mdmem + x, bp->b_bcount);
    bp->b_resid = 0;
    iodone(bp);
}

mdread(dev)
{
    physio(mdstrategy, &rmdbuf, dev, B_READ);
}

mdwrite(dev)
{
    physio(mdstrategy, &rmdbuf, dev, B_WRITE);
}
