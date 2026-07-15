/* V7/x86 source code: see www.nordier.com/v7x86 for details. */
/* Copyright (c) 2007 Robert Nordier.  All rights reserved. */

/*
 * BESM-6 drum / disk driver -- SKELETON.
 *
 * This was the x86 ATA/IDE driver.  Its low-level transfer path was pure x86
 * programmed I/O (inb/outb/insw/outsw against the controller ports), which the
 * BESM-6 does not have: mass storage is driven through the 033 «увв» channel and
 * answers in ГРП (doc/Besm6_Peripherals.md).  All of that x86 code is gone; what
 * remains is the driver's public surface -- the entry points conf.c wires into
 * bdevsw/cdevsw -- as stubs, so the kernel builds and links while the real
 * 033-channel driver is written.
 */

// clang-format off
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/buf.h"
// clang-format on

struct buf rhdbuf;
struct buf hdtab;

void hdopen(dev_t dev, int rw)
{
    /* TODO: BESM-6 drum/disk open (unit / partition setup). */
}

void hdstrategy(register struct buf *bp)
{
    /*
     * TODO: BESM-6 drum/disk via the 033 channel (doc/Besm6_Peripherals.md).
     * Until that exists, fail every request rather than drive x86 hardware.
     */
    bp->b_flags |= B_ERROR;
    iodone(bp);
}

void hdintr()
{
    /* TODO: BESM-6 drum/disk transfer-complete interrupt (ГРП). */
}

void hdread(int dev)
{
    physio(hdstrategy, &rhdbuf, dev, B_READ);
}

void hdwrite(int dev)
{
    physio(hdstrategy, &rhdbuf, dev, B_WRITE);
}
