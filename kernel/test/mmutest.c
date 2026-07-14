/*
 * mmutest -- the BESM-6 MMU, driven by the kernel's own sureg() (kernel/utab.c).
 *
 * A standalone SIMH program: crt0.s brings the machine up and calls main(), and we
 * link the real utab.o and brz.o against a hand-built process.  Everything here runs
 * in supervisor mode -- reset leaves РежЭ set -- which is what makes `mod' (002 рег)
 * legal, so the kernel's address-space code can be exercised with no kernel under it.
 *
 * The map it builds:
 *
 *      virtual page   0   1 |  2   3 | 4 .. 27 | 28 | 29 30 31
 *      physical page  20  21|  17  18|  --     | 19 |  --
 *                     text  |  data  | unmapped| stk| unmapped
 *
 * Shared text at physical page 20, the process image at page 16: the u-area page
 * (which is NOT in the map -- it is physical), then two data pages, then one stack
 * page.  Every physical page it uses is below 32, so the test can read them back with
 * ordinary unmapped loads and compare.
 *
 * main() returns 0 on success; a nonzero return names the check that failed, and
 * mmutest.ini asserts on it along with the twelve registers sureg() wrote.
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

/*
 * The kernel globals utab.o refers to.  In the kernel `u' is an absolute symbol at
 * 076000 and maxmem is counted by startup(); here they are just storage.
 */
struct user u;
int maxmem = 512 * 1024; /* words: a fully populated machine */

static struct proc pr;
static struct text tx;

/* mmuhelp.s */
unsigned peek(unsigned vaddr);
void poke(unsigned vaddr, unsigned val);

/* brz.s */
void drainbrz(void);

/*
 * crt0.s's 0501 vector calls this.  We never enable external interrupts, so it can
 * never run; it exists because the vector names it.
 */
void extintr(void)
{
}

#define TEXTPG  20 /* physical page of the shared text */
#define IMAGEPG 16 /* physical page of the process image (its u-area page) */

#define DATAPG (IMAGEPG + 1) /* the image is u, then data, then stack */
#define STKPG  (IMAGEPG + 3)

#define PATTERN1 0525252
#define PATTERN2 0123456

int main()
{
    unsigned va, pa;

    /*
     * Task 6: struct user and a kernel stack must share the one u page.
     * sizeof() is in char units, six to a word.
     */
    if (sizeof(struct user) / sizeof(int) >= 200)
        return (1);

    tx.x_caddr = TEXTPG * PGSZ;
    tx.x_size  = 2 * PGSZ;

    pr.p_addr  = IMAGEPG * PGSZ;
    pr.p_size  = USIZE + 3 * PGSZ;
    pr.p_textp = &tx;

    u.u_procp = &pr;
    u.u_tsize = 2 * PGSZ;
    u.u_dsize = 2 * PGSZ;
    u.u_ssize = PGSZ;

    sureg();

    /*
     * The shadow map, read back through physaddr().
     */
    if (physaddr(0) != TEXTPG * PGSZ)
        return (2);
    if (physaddr(2 * PGSZ + 5) != DATAPG * PGSZ + 5)
        return (3);
    if (physaddr(USTKPAGE * PGSZ + 1) != STKPG * PGSZ + 1)
        return (4);
    if (physaddr(10 * PGSZ) != 0) /* a page in the hole is not mapped */
        return (5);

    /*
     * useracc(): the range must lie entirely in mapped pages.  Words in words.
     */
    if (!useracc(2 * PGSZ, 2 * PGSZ, 0)) /* both data pages */
        return (6);
    if (useracc(3 * PGSZ, 2 * PGSZ, 0)) /* runs off the data into the hole */
        return (7);
    if (useracc(USTKPAGE * PGSZ, PGSZ, 0) == 0) /* the stack page */
        return (8);

    /*
     * The mapping is real, not just self-consistent: write through a VIRTUAL
     * address and read the PHYSICAL word back.
     *
     * The drain is not optional here, and that is the point.  poke() stores with
     * mapping on, so the dirty БРЗ line is tagged with the virtual address; the
     * physical read below carries a different tag and would miss it and see stale
     * memory.  drainbrz() writes the line back through the map that is still loaded,
     * which is exactly the hazard a context switch faces.  Under `set mmu cache'
     * this check fails without it.
     */
    va = 2 * PGSZ + 5;
    pa = DATAPG * PGSZ + 5;
    poke(va, PATTERN1);
    drainbrz();
    if (*(volatile unsigned *)pa != PATTERN1)
        return (9);
    if (peek(va) != PATTERN1)
        return (10);

    /* ...and once more at the far end of the map, through the stack page. */
    va = USTKPAGE * PGSZ + 3;
    pa = STKPG * PGSZ + 3;
    poke(va, PATTERN2);
    drainbrz();
    if (*(volatile unsigned *)pa != PATTERN2)
        return (11);

    /* A word the map does not reach must not have been touched. */
    if (*(volatile unsigned *)(DATAPG * PGSZ + 6) != 0)
        return (12);

    return (0);
}

/*
 * utab.o's clearseg()/copyseg() call these; nothing here does.  They are stubs, and
 * task 11 replaces the callers with assembly brackets anyway.
 */
void bzero(void *dst, unsigned len)
{
    register char *p = (char *)dst;

    while (len--)
        *p++ = 0;
}

void bcopy(const void *src, void *dst, unsigned len)
{
    register const char *from = (const char *)src;
    register char *to         = (char *)dst;

    while (len--)
        *to++ = *from++;
}
