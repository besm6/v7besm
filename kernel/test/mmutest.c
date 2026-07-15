/*
 * mmutest -- the BESM-6 MMU, driven by the kernel's own sureg() (kernel/utab.c).
 *
 * A standalone SIMH program: crt0.s brings the machine up and calls main(), and we
 * link the real utab.o, brz.o and uarea.o against a hand-built process.  Everything runs
 * in supervisor mode -- reset leaves РежЭ set -- which is what makes `mod' (002 рег)
 * legal, so the kernel's address-space code can be exercised with no kernel under it.
 *
 * Two halves: sureg() builds and loads a map (checks 1-12), and then uflush()/uload()
 * round-trip a u-area through a page above 0100000 (checks 13-17, task 10).
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
#define PATTERN3 0707070

/*
 * The u-area round trip (task 10).  UHOME is a page above 0100000 -- out of reach of any
 * unmapped access, which is the whole reason uflush()/uload() have to window it -- and clear
 * both of this image (which lives in the low pages) and of the map built above.
 */
#define UHOMEPG 40
#define UHOME   (UHOMEPG * PGSZ)

/* Two more pool pages above 0100000, for the copyseg()/clearseg() leg (task 11). */
#define SEGSRC 41
#define SEGDST 42

/*
 * The word offset of u_upt in struct user.  kernel/uarea.s is assembled by bare b6as, which
 * cannot compute an offsetof(), so it hardcodes this -- and this is what keeps it honest.
 */
#define UPT 33

int main()
{
    unsigned va, pa;
    volatile unsigned *up;
    int i;

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

    /*
     * Task 10: the u-area round trip, through uflush()/uload() (kernel/uarea.s).
     *
     * The assembly hardcodes the offset of u_upt -- b6as cannot compute an offsetof() -- so
     * check it here, where the compiler can.  Get this wrong and the brackets would restore
     * garbage into РП0..3, which is a much more confusing failure than this one.
     */
    if ((char *)u.u_upt - (char *)&u != UPT * sizeof(int))
        return (13);

    /*
     * Fill the live u-area at UBASE.  The kernel reaches it unmapped, and so can we: 076000 is
     * word 32256, inside the 15-bit word field of a pointer.  The pattern is non-zero at word
     * 0 on purpose -- a window on virtual page 0 would silently drop exactly that word, which
     * is why uarea.s windows pages 1 and 2 instead.
     */
    up = (volatile unsigned *)UBASE;
    for (i = 0; i < USIZE; i++)
        up[i] = PATTERN2 ^ i;
    drainbrz(); /* settle it into memory: the point of the next paragraph is what is NOT settled */

    for (i = 0; i < 8; i++)
        up[i] = PATTERN1 ^ i;

    /*
     * Leave the write cache dirty with VIRTUAL tags, and do not drain: these eight stores were
     * made through the map, so their БРЗ lines are tagged with virtual addresses in page 2.
     * uflush() is about to point virtual page 2 somewhere else entirely (at the live u-area), so
     * if it does not drain first, these lines are written back through the STOLEN map and land
     * on the wrong physical page.  That is the hazard the leading drain exists for, and it is
     * the one a context switch faces every time it reloads РП with a user's stores outstanding.
     */
    for (i = 0; i < 8; i++)
        poke(2 * PGSZ + 8 + i, PATTERN3 ^ i);

    uflush(UHOME);

    /* Drained through the map that was loaded when they were made, so they reached the data page. */
    drainbrz();
    for (i = 0; i < 8; i++)
        if (*(volatile unsigned *)(DATAPG * PGSZ + 8 + i) != (unsigned)(PATTERN3 ^ i))
            return (17);

    /*
     * The window is gone again.  Virtual page 2 is the first data page of the map above, and
     * it is one of the two pages uflush() steals -- so this reads РП itself, not the shadow.
     */
    if (peek(2 * PGSZ + 5) != PATTERN1)
        return (14);

    /* Scribble, so that a uload() that copied nothing would be caught. */
    for (i = 0; i < USIZE; i++)
        up[i] = PATTERN3 ^ i;

    uload(UHOME);

    /*
     * The whole page must come back as it was flushed: the dirty head, then the settled tail.
     * Word 0 included -- it is the one a virtual-page-0 window would have lost.
     */
    for (i = 0; i < USIZE; i++)
        if (up[i] != (unsigned)((i < 8 ? PATTERN1 : PATTERN2) ^ i))
            return (15);

    /*
     * And the copy really went to physical UHOME, above 0100000 -- not to some page the two
     * routines happen to share.  Map the home page at virtual page 0 and read it back.  Word 5,
     * not word 0: virtual address 0 is the black hole.
     */
    tx.x_caddr = UHOME;
    sureg();
    if (peek(5) != (unsigned)(PATTERN1 ^ 5))
        return (16);

    /*
     * Task 11: copyseg()/clearseg() (kernel/seg.s), reaching a page above 0100000.
     *
     * SEGSRC and SEGDST are two pool pages out of reach of any unmapped access -- the whole
     * reason the two routines have to window them.  Fill the live page at UBASE (which the
     * kernel, and we, reach unmapped) with a source pattern and DO NOT drain: copyseg's own
     * leading drain has to settle those unmapped, physical-tagged stores before it reads the
     * page back mapped, or it copies stale memory.  That is the hazard this leg exists for.
     */
    up = (volatile unsigned *)UBASE;
    for (i = 0; i < PGSZ; i++)
        up[i] = PATTERN1 ^ i;

    copyseg(UBASE, SEGSRC * PGSZ);          /* low -> high: settles the fill, windows both pages */
    copyseg(SEGSRC * PGSZ, SEGDST * PGSZ);  /* high -> high: a page above 0100000 to another */

    /*
     * Read SEGDST back.  Map it at virtual page 0 and peek sample words -- not word 0, the
     * black hole.  copyseg's trailing drain is what put its mapped stores into physical memory;
     * without it this reads stale.
     */
    tx.x_caddr = SEGDST * PGSZ;
    sureg();
    if (peek(5) != (unsigned)(PATTERN1 ^ 5))
        return (18);
    if (peek(500) != (unsigned)(PATTERN1 ^ 500))
        return (18);
    if (peek(PGSZ - 1) != (unsigned)(PATTERN1 ^ (PGSZ - 1)))
        return (18);

    /* clearseg() zeroes the same page. */
    clearseg(SEGDST * PGSZ);
    tx.x_caddr = SEGDST * PGSZ;
    sureg();
    if (peek(5) != 0)
        return (19);
    if (peek(PGSZ - 1) != 0)
        return (19);

    /* Put the map back, so the .ini's РП/РЗ assertions describe the state it expects. */
    tx.x_caddr = TEXTPG * PGSZ;
    sureg();

    return (0);
}
