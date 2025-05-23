/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 1999 Robert Nordier. All rights reserved. */

// clang-format off
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/map.h"
// clang-format on

/*
 * Allocate 'size' units from the given
 * map. Return the base of the allocated
 * space.
 * In a map, the addresses are increasing and the
 * list is terminated by a 0 size.
 * The core map unit is 4096 bytes; the swap map unit
 * is 512 bytes.
 * Algorithm is first-fit.
 */
int malloc(struct map *mp, int size)
{
    register unsigned int a;
    register struct map *bp;

    for (bp = mp; bp->m_size; bp++) {
        if (bp->m_size >= size) {
            a = bp->m_addr;
            bp->m_addr += size;
            if ((bp->m_size -= size) == 0) {
                do {
                    bp++;
                    (bp - 1)->m_addr = bp->m_addr;
                } while (((bp - 1)->m_size = bp->m_size));
            }
            return (a);
        }
    }
    return (0);
}

/*
 * Free the previously allocated space aa
 * of size units into the specified map.
 * Sort aa into map and combine on
 * one or both ends if possible.
 */
void mfree(struct map *mp, int size, register int a)
{
    register struct map *bp;
    register unsigned int t;

    if ((bp = mp) == coremap && runin) {
        runin = 0;
        wakeup((caddr_t)&runin); /* Wake scheduler when freeing core */
    }
    for (; bp->m_addr <= a && bp->m_size != 0; bp++)
        ;
    if (bp > mp && (bp - 1)->m_addr + (bp - 1)->m_size == a) {
        (bp - 1)->m_size += size;
        if (a + size == bp->m_addr) {
            (bp - 1)->m_size += bp->m_size;
            while (bp->m_size) {
                bp++;
                (bp - 1)->m_addr = bp->m_addr;
                (bp - 1)->m_size = bp->m_size;
            }
        }
    } else {
        if (a + size == bp->m_addr && bp->m_size) {
            bp->m_addr -= size;
            bp->m_size += size;
        } else if (size) {
            do {
                t          = bp->m_addr;
                bp->m_addr = a;
                a          = t;
                t          = bp->m_size;
                bp->m_size = size;
                bp++;
            } while ((size = t));
        }
    }
}
