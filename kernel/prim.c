/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

// clang-format off
#include "sys/param.h"
#include "sys/tty.h"
#include "sys/systm.h"
#include "sys/conf.h"
#include "sys/buf.h"
// clang-format on

struct cblock {
    struct cblock *c_next;
    char c_info[CBSIZE];
};

struct cblock cfree[NCLIST];
struct cblock *cfreelist;
int cbad;
int nchrdev;

/*
 * Character list get/put
 */
int getc(register struct clist *p)
{
    register struct cblock *bp;
    register int c, s;

    s = spl6();
    if (p->c_cc <= 0) {
        c       = -1;
        p->c_cc = 0;
        p->c_cf = p->c_cl = NULL;
    } else {
        c = *p->c_cf++ & 0377;
        if (--p->c_cc <= 0) {
            bp         = (struct cblock *)(p->c_cf - 1);
            bp         = (struct cblock *)((int)bp & ~CROUND);
            p->c_cf    = NULL;
            p->c_cl    = NULL;
            bp->c_next = cfreelist;
            cfreelist  = bp;
        } else if (((int)p->c_cf & CROUND) == 0) {
            bp = (struct cblock *)(p->c_cf);
            bp--;
            p->c_cf    = bp->c_next->c_info;
            bp->c_next = cfreelist;
            cfreelist  = bp;
        }
    }
    splx(s);
    return (c);
}

/*
 * copy clist to buffer.
 * return number of bytes moved.
 */
int q_to_b(register struct clist *q, register char *cp, int cc)
{
    register struct cblock *bp;
    register int s;
    char *acp;

    if (cc <= 0)
        return (0);
    s = spl6();
    if (q->c_cc <= 0) {
        q->c_cc = 0;
        q->c_cf = q->c_cl = NULL;
        return (0);
    }
    acp = cp;
    cc++;

    while (--cc) {
        *cp++ = *q->c_cf++;
        if (--q->c_cc <= 0) {
            bp      = (struct cblock *)(q->c_cf - 1);
            bp      = (struct cblock *)((int)bp & ~CROUND);
            q->c_cf = q->c_cl = NULL;
            bp->c_next        = cfreelist;
            cfreelist         = bp;
            break;
        }
        if (((int)q->c_cf & CROUND) == 0) {
            bp = (struct cblock *)(q->c_cf);
            bp--;
            q->c_cf    = bp->c_next->c_info;
            bp->c_next = cfreelist;
            cfreelist  = bp;
        }
    }
    splx(s);
    return (cp - acp);
}

/*
 * Return count of contiguous characters
 * in clist starting at q->c_cf.
 * Stop counting if flag&character is non-null.
 */
int ndqb(register struct clist *q, int flag)
{
    register int cc;
    int s;

    s = spl6();
    if (q->c_cc <= 0) {
        cc = -q->c_cc;
        goto out;
    }
    cc = ((int)q->c_cf + CBSIZE) & ~CROUND;
    cc -= (int)q->c_cf;
    if (q->c_cc < cc)
        cc = q->c_cc;
    if (flag) {
        register char *p, *end;

        p   = q->c_cf;
        end = p;
        end += cc;
        while (p < end) {
            if (*p & flag) {
                cc = (int)p;
                cc -= (int)q->c_cf;
                break;
            }
            p++;
        }
    }
out:
    splx(s);
    return (cc);
}

/*
 * Update clist to show that cc characters
 * were removed.  It is assumed that cc < CBSIZE.
 */
void ndflush(register struct clist *q, register int cc)
{
    register int s;

    s = spl6();
    if (q->c_cc < 0) {
        if (q->c_cf != NULL) {
            q->c_cc += cc;
            q->c_cf += cc;
            goto out;
        }
        q->c_cc = 0;
        goto out;
    }
    if (q->c_cc == 0) {
        goto out;
    }
    if (cc > CBSIZE || cc <= 0) {
        cbad++;
        goto out;
    }
    q->c_cc -= cc;
    q->c_cf += cc;
    if (((int)q->c_cf & CROUND) == 0) {
        register struct cblock *bp;

        bp = (struct cblock *)(q->c_cf) - 1;
        if (bp->c_next) {
            q->c_cf = bp->c_next->c_info;
        } else {
            q->c_cf = q->c_cl = NULL;
        }
        bp->c_next = cfreelist;
        cfreelist  = bp;
    } else if (q->c_cc == 0) {
        register struct cblock *bp;
        q->c_cf    = (char *)((int)q->c_cf & ~CROUND);
        bp         = (struct cblock *)(q->c_cf);
        bp->c_next = cfreelist;
        cfreelist  = bp;
        q->c_cf = q->c_cl = NULL;
    }
out:
    splx(s);
}

int putc(int c, register struct clist *p)
{
    register struct cblock *bp;
    register char *cp;
    register int s;

    s = spl6();
    if ((cp = p->c_cl) == NULL || p->c_cc < 0) {
        if ((bp = cfreelist) == NULL) {
            splx(s);
            return (-1);
        }
        cfreelist  = bp->c_next;
        bp->c_next = NULL;
        p->c_cf = cp = bp->c_info;
    } else if (((int)cp & CROUND) == 0) {
        bp = (struct cblock *)cp - 1;
        if ((bp->c_next = cfreelist) == NULL) {
            splx(s);
            return (-1);
        }
        bp         = bp->c_next;
        cfreelist  = bp->c_next;
        bp->c_next = NULL;
        cp         = bp->c_info;
    }
    *cp++ = c;
    p->c_cc++;
    p->c_cl = cp;
    splx(s);
    return (0);
}

/*
 * copy buffer to clist.
 * return number of bytes not transfered.
 */
int b_to_q(register char *cp, register int cc, struct clist *q)
{
    register char *cq;
    register struct cblock *bp;
    register int s, acc;

    if (cc <= 0)
        return (0);
    acc = cc;

    s = spl6();
    if ((cq = q->c_cl) == NULL || q->c_cc < 0) {
        if ((bp = cfreelist) == NULL)
            goto out;
        cfreelist  = bp->c_next;
        bp->c_next = NULL;
        q->c_cf = cq = bp->c_info;
    }

    while (cc) {
        if (((int)cq & CROUND) == 0) {
            bp = (struct cblock *)cq - 1;
            if ((bp->c_next = cfreelist) == NULL)
                goto out;
            bp         = bp->c_next;
            cfreelist  = bp->c_next;
            bp->c_next = NULL;
            cq         = bp->c_info;
        }
        *cq++ = *cp++;
        cc--;
    }
out:
    q->c_cl = cq;
    q->c_cc += acc - cc;
    splx(s);
    return (cc);
}

/*
 * Initialize clist by freeing all character blocks, then count
 * number of character devices. (Once-only routine)
 */
void cinit()
{
    register int ccp;
    register struct cblock *cp;
    register struct cdevsw *cdp;

    ccp = (int)cfree;
    ccp = (ccp + CROUND) & ~CROUND;
    for (cp = (struct cblock *)ccp; cp <= &cfree[NCLIST - 1]; cp++) {
        cp->c_next = cfreelist;
        cfreelist  = cp;
    }
    ccp = 0;
    for (cdp = cdevsw; cdp->d_open; cdp++)
        ccp++;
    nchrdev = ccp;
}

/*
 * integer (2-byte) get/put
 * using clists
 */
int getw(register struct clist *p)
{
    register int s;

    if (p->c_cc <= 1)
        return (-1);
    s = getc(p);
    return (s | (getc(p) << 8));
}

int putw(int c, register struct clist *p)
{
    register int s;

    s = spl6();
    if (cfreelist == NULL) {
        splx(s);
        return (-1);
    }
    putc(c, p);
    putc(c >> 8, p);
    splx(s);
    return (0);
}
