/* V7/x86 source code: see www.nordier.com/v7x86 for details. */
/* Copyright (c) 1999 Robert Nordier.  All rights reserved. */

/*
 * Per-process address-space setup.  Still x86 paging: a two-level page table
 * (pdir/upt) of 4K pages, plus the PHY window onto physical memory.
 *
 * On the BESM-6 none of that exists -- the whole mapping is eight write-only
 * page registers RP (002 020-027) and the protection register RZ (002 030-033),
 * over 32 virtual pages of 1 Kword.  sureg() becomes twelve `reg' writes, invd()
 * becomes a no-op, and physaddr()/pdir[] have no counterpart at all: the mapping
 * cannot be read back, so the kernel must keep its own shadow copy.
 * See doc/Memory_Mapping.md before retargeting this file.
 */

// clang-format off
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/dir.h"
#include "sys/user.h"
#include "sys/proc.h"
#include "sys/text.h"
#include "sys/seg.h"
// clang-format on

extern int pdir[], upt[];

#define NT 0
#define ND 1
#define NS 2
#define XX 3

int uplo;
int uphi;

/* sutab and estabut */

void sureg()
{
    register int i, a, n;
    int taddr, daddr;
    struct text *tp;

    for (i = uplo, uplo = u.u_utab[NT] + u.u_utab[ND]; i > uplo;)
        upt[--i] = 0;
    for (i = uphi, uphi = 1023 - u.u_utab[NS]; i < uphi;)
        upt[++i] = 0;
    taddr = daddr = u.u_procp->p_addr;
    if ((tp = u.u_procp->p_textp) != NULL)
        taddr = tp->x_caddr;
    taddr *= PGSZ;
    daddr *= PGSZ;
    i = 0;
    a = taddr;
    for (n = u.u_utab[NT]; n--; a += PGSZ)
        upt[i++] = a | u.u_utab[XX];
    a = daddr + USIZE * PGSZ;
    for (n = u.u_utab[ND]; n--; a += PGSZ)
        upt[i++] = a | 7;
    i = uphi;
    for (n = u.u_utab[NS]; n--; a += PGSZ)
        upt[++i] = a | 7;
    invd();
}

int estabur(int nt, int nd, int ns, int sep, int xrw)
{
    if (nt + nd + ns > 1023)
        goto err;
    if (nt + nd + ns + USIZE > maxmem)
        goto err;
    u.u_utab[NT] = nt;
    u.u_utab[ND] = nd;
    u.u_utab[NS] = ns;
    u.u_utab[XX] = xrw;
    sureg();
    return (0);
err:
    u.u_error = ENOMEM;
    return (-1);
}

void clearseg(int d)
{
    unsigned xd;

    xd = PHY + ctob(d);
    bzero((caddr_t)xd, PGSZ);
}

void copyseg(int s, int d)
{
    unsigned xs, xd;

    if (s == d)
        return;
    xs = PHY + ctob(s);
    xd = PHY + ctob(d);
    bcopy((caddr_t)xs, (caddr_t)xd, PGSZ);
}

unsigned physaddr(unsigned addr)
{
    unsigned d, t, o, x, z;
    unsigned *pt;

    d  = addr >> 22;
    t  = (addr >> 12) & 1023;
    o  = addr & 4095;
    x  = 0;
    z  = 0;
    pt = (unsigned *)(PHY + pdir[d] & ~4095);
    if (pt != NULL) {
        x = pt[t] & ~4095;
        z = x + o;
    }
    return z;
}
