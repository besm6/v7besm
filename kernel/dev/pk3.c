/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

// clang-format off
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/conf.h"
#include "sys/buf.h"
// clang-format on

#define XPRI    30
#define NBLOCKS 10

int bwaiting, wcount;
struct buf *bufps[NBLOCKS];
char *nbase[NBLOCKS]; /* normal allocations */
short nmap[NBLOCKS];  /* 1 bit == 32 bytes */

char log[] = { 0, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 4 };

#define FULL   -1
#define LOCK   s = spl6()
#define UNLOCK splx(s)

/*
 * getepack: get empty packet
 * with size specified by bitmask.
 */
char *getepack(int bits)
{
    register int i;
    int s, savbits;
    char **base;
    short *map;

    base    = nbase;
    map     = nmap;
    savbits = bits;

    /*
     * search for space
     */
    LOCK;
    for (;;) {
        if (bits == FULL)
            goto force;

        for (i = 0; i < NBLOCKS; i++) {
            register int m;
            register unsigned n;
            register int offset;

            m = map[i];
            if (m == FULL || base[i] == NULL)
                continue;
            if (bits == 1) {
                n = m;
                m |= m + 1;
                n = m - n;
                for (offset = 0; n > 16; n >>= 4)
                    offset += 4;
                offset += log[n];
            } else {
                bits = savbits;
                for (n = 17; --n; bits <<= 1)
                    if ((m & bits) == 0)
                        goto found;
                continue;
            found:
                offset = 16 - n;
                m |= bits;
            }
            map[i] = m;
            UNLOCK;
            return (base[i] + 32 * offset);
        }
        /*
         * grab another block from the system
         */
    force:
        for (i = 0; i < NBLOCKS; i++) {
            register struct buf *bp;

            if (bufps[i] != NULL)
                continue;
            bufps[i] = bp = geteblk();
            bp->b_flags |= B_PACK;
            bp->b_flags |= B_PBUSY;
            map[i]  = bits;
            base[i] = bp->b_un.b_addr;
            UNLOCK;
            return (bp->b_un.b_addr);
        }
        /*
         * sleep until something is released
         */
        bwaiting++;
        wcount++;
        sleep((caddr_t)&bwaiting, XPRI);
        bwaiting--;
    }
}

/*
 * freepack: release space beginning
 * at address p with length specified
 * by bits.
 */
void freepack(char *p, int bits)
{
    register int i, d, s;
    char **base;
    short *map;

    if (p == NULL)
        return;
    LOCK;
    base = nbase;
    map  = nmap;

    for (i = 0; i < NBLOCKS; i++) {
        d = p - base[i];
        if (d >= 0 && d <= 512)
            goto found;
    }
    goto out;
found:
    d >>= 5;
    d = (bits << d);
    map[i] &= ~d;
    if (map[i] == 0) {
        register struct buf *bp;

        bp = bufps[i];
        bp->b_flags &= ~B_PBUSY;
        base[i]  = NULL;
        bufps[i] = NULL;
        brelse(bp);
    }
    if (bwaiting)
        wakeup((caddr_t)&bwaiting);
out:
    splx(s);
}

/*
 * integer to bitmap conversion
 */
int dtom(register int d)
{
    register int m;

    m = 1;
    while (d > 32) {
        d -= 32;
        m |= m + 1;
    }
    return (m);
}

#define NRECS 160

int reclist[NRECS];
int recbits[NRECS];
