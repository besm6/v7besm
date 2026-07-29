// mmutest -- the BESM-6 MMU, driven by the kernel's own sureg() (kernel/utab.c).
//
// A standalone SIMH program: crt0.s brings the machine up and calls main(), and we
// link the real utab.o, brz.o and uarea.o against a hand-built process.  Everything runs
// in supervisor mode -- reset leaves РежЭ set -- which is what makes `mod' (002 рег)
// legal, so the kernel's address-space code can be exercised with no kernel under it.
//
// Two halves: sureg() builds and loads a map (checks 1-12), and then the mapped brackets --
// uflush()/uload() round-tripping a u-area through a page above 0100000 (checks 13-17, task
// 10), copyseg()/clearseg() (18-19, task 11), copyphys() (26-28, task 27) -- and the
// user-memory copies on top of them (20-25, task 12).
//
// The map it builds:
//
//      virtual page   0   1 |  2   3 | 4 .. 27 | 28 | 29 30 31
//      physical page  20  21|  17  18|  --     | 19 |  --
//                     text  |  data  | unmapped| stk| unmapped
//
// Shared text at physical page 20, the process image at page 16: the u-area page
// (which is NOT in the map -- it is physical), then two data pages, then one stack
// page.  Every physical page it uses is below 32, so the test can read them back with
// ordinary unmapped loads and compare.
//
// main() returns 0 on success; a nonzero return names the check that failed, and
// mmutest.ini asserts on it along with the twelve registers sureg() wrote.

// clang-format off
#include "sys/types.h"
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/dir.h"
#include "sys/user.h"
#include "sys/proc.h"
#include "sys/text.h"
#include "sys/seg.h"
// clang-format on

// The kernel globals utab.o refers to.  In the kernel `u' is an absolute symbol at
// 074000 and maxmem is counted by startup(); here they are just storage.
struct user u;
int maxmem = 512 * 1024; // words: a fully populated machine

static struct proc pr;
static struct text tx;

// A stand-in for one exec argument.  Ten bytes including the NUL, so it spans two words
// and its copy has to make the carry b$pinc exists for (offset 0 -> 5, word + 1).
static char argstr[] = "/etc/init";

// mmuhelp.s
unsigned peek(unsigned vaddr);
void poke(unsigned vaddr, unsigned val);

// brz.s
void drainbrz(void);

// crt0.s's 0501 vector calls this.  We never enable external interrupts, so it can
// never run; it exists because the vector names it.
void extintr(void)
{
}

#define TEXTPG  20 // physical page of the shared text
#define IMAGEPG 16 // physical page of the process image (its u-area page)

#define DATAPG (IMAGEPG + 1) // the image is u, then data, then stack
#define STKPG  (IMAGEPG + 3)

#define PATTERN1 0525252
#define PATTERN2 0123456
#define PATTERN3 0707070

// A char* into user virtual word `w': cast the word address to int* and then to char* -- the
// compiler makes it a fat pointer at byte #0 (the MSB byte), and `+ k' walks toward the LSB, byte
// by byte, exactly as exec/namei do (doc/Besm6_Data_Representation.md §7).  This is the real path
// fubyte()/subyte() take; building the fat pointer by hand from an int misses the marker bit.
#define UBYTE(w, k) ((caddr_t)(int *)(w) + (k))

// The u-area round trip (task 10).  UHOME is a page above 0100000 -- out of reach of any
// unmapped access, which is the whole reason uflush()/uload() have to window it -- and clear
// both of this image (which lives in the low pages) and of the map built above.
#define UHOMEPG 40
#define UHOME   (UHOMEPG * PGSZ)

// Two more pool pages above 0100000, for the copyseg()/clearseg() leg (task 11).
#define SEGSRC 41
#define SEGDST 42

// The word offsets of u_upt and u_stkdepth in struct user.  kernel/uarea.S is preprocessed
// but not compiled, so it can pick up #defines from sys/param.h yet still cannot compute an
// offsetof() -- it hardcodes these, and this is what keeps it honest.  u_stkdepth sits
// immediately after u_upt[8] so that the two can only drift together (task 30).
#define UPT   33
#define USTKD (UPT + 8)

int main()
{
    unsigned va, pa;
    volatile unsigned *up;
    unsigned kbuf[4];
    unsigned uw, w;
    char *cq;
    int i;

    // Task 6: struct user and a kernel stack must share the one u page.
    // sizeof() is in char units, six to a word.
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

    // The shadow map, read back through physaddr().
    if (physaddr(0) != TEXTPG * PGSZ)
        return (2);
    if (physaddr(2 * PGSZ + 5) != DATAPG * PGSZ + 5)
        return (3);
    if (physaddr(USTKPAGE * PGSZ + 1) != STKPG * PGSZ + 1)
        return (4);
    if (physaddr(10 * PGSZ) != 0) // a page in the hole is not mapped
        return (5);

    // useracc(): the range must lie entirely in mapped pages.  Words in words.
    if (!useracc(2 * PGSZ, 2 * PGSZ, 0)) // both data pages
        return (6);
    if (useracc(3 * PGSZ, 2 * PGSZ, 0)) // runs off the data into the hole
        return (7);
    if (useracc(USTKPAGE * PGSZ, PGSZ, 0) == 0) // the stack page
        return (8);

    // The mapping is real, not just self-consistent: write through a VIRTUAL
    // address and read the PHYSICAL word back.
    //
    // The drain is not optional here, and that is the point.  poke() stores with
    // mapping on, so the dirty БРЗ line is tagged with the virtual address; the
    // physical read below carries a different tag and would miss it and see stale
    // memory.  drainbrz() writes the line back through the map that is still loaded,
    // which is exactly the hazard a context switch faces.  Under `set mmu cache'
    // this check fails without it.
    va = 2 * PGSZ + 5;
    pa = DATAPG * PGSZ + 5;
    poke(va, PATTERN1);
    drainbrz();
    if (*(volatile unsigned *)pa != PATTERN1)
        return (9);
    if (peek(va) != PATTERN1)
        return (10);

    // ...and once more at the far end of the map, through the stack page.
    va = USTKPAGE * PGSZ + 3;
    pa = STKPG * PGSZ + 3;
    poke(va, PATTERN2);
    drainbrz();
    if (*(volatile unsigned *)pa != PATTERN2)
        return (11);

    // A word the map does not reach must not have been touched.
    if (*(volatile unsigned *)(DATAPG * PGSZ + 6) != 0)
        return (12);

    // Task 10: the u-area round trip, through uflush()/uload() (kernel/uarea.S).
    //
    // The assembly hardcodes the offset of u_upt -- b6as cannot compute an offsetof() -- so
    // check it here, where the compiler can.  Get this wrong and the brackets would restore
    // garbage into РП0..3, which is a much more confusing failure than this one.
    if ((char *)u.u_upt - (char *)&u != UPT * sizeof(int))
        return (13);
    if ((char *)&u.u_stkdepth - (char *)&u != USTKD * sizeof(int))
        return (18);

    // Fill the live u-area at UBASE.  The kernel reaches it unmapped, and so can we: 074000 is
    // word 30720, inside the 15-bit word field of a pointer.  The pattern is non-zero at word
    // 0 on purpose -- a window on virtual page 0 would silently drop exactly that word, which
    // is why uarea.S windows pages 1 and 2 instead.
    //
    // USIZE words, so this is exactly the SAVED page of the u-area (074000-075777).  A switch
    // in the kernel copies only as far as r15 has reached (task 30), but THIS program's stack
    // is in low bss (crt0.s), nowhere near the u-area, so uflush takes its out-of-range arm
    // and moves the whole page -- which is what makes the round trip below a full-page one,
    // and makes this leg the only test of that arm.  The overflow page above it (076000-077777,
    // task 25a) is not part of any process image and is deliberately not exercised here: there
    // is nothing to round-trip.  Note that this leg is what pins uarea.S's live-window
    // descriptor to UBASE: window the wrong physical page and the fill and the copy no longer
    // name the same words.
    up = (volatile unsigned *)UBASE;
    for (i = 0; i < USIZE; i++)
        up[i] = PATTERN2 ^ i;
    drainbrz(); // settle it into memory: the point of the next paragraph is what is NOT settled

    for (i = 0; i < 8; i++)
        up[i] = PATTERN1 ^ i;

    // Leave the write cache dirty with VIRTUAL tags, and do not drain: these eight stores were
    // made through the map, so their БРЗ lines are tagged with virtual addresses in page 2.
    // uflush() is about to point virtual page 2 somewhere else entirely (at the live u-area), so
    // if it does not drain first, these lines are written back through the STOLEN map and land
    // on the wrong physical page.  That is the hazard the leading drain exists for, and it is
    // the one a context switch faces every time it reloads РП with a user's stores outstanding.
    for (i = 0; i < 8; i++)
        poke(2 * PGSZ + 8 + i, PATTERN3 ^ i);

    uflush(UHOME);

    // Drained through the map that was loaded when they were made, so they reached the data page.
    drainbrz();
    for (i = 0; i < 8; i++)
        if (*(volatile unsigned *)(DATAPG * PGSZ + 8 + i) != (unsigned)(PATTERN3 ^ i))
            return (17);

    // The window is gone again.  Virtual page 2 is the first data page of the map above, and
    // it is one of the two pages uflush() steals -- so this reads РП itself, not the shadow.
    if (peek(2 * PGSZ + 5) != PATTERN1)
        return (14);

    // Scribble, so that a uload() that copied nothing would be caught.
    for (i = 0; i < USIZE; i++)
        up[i] = PATTERN3 ^ i;

    uload(UHOME);

    // The whole page must come back as it was flushed: the dirty head, then the settled tail.
    // Word 0 included -- it is the one a virtual-page-0 window would have lost.
    //
    // Except word USTKD, which uflush overwrote in the home with the number of words it
    // copied and uload has just brought back.  Asserting it is USIZE is asserting that the
    // out-of-range arm ran: nothing else in the suite has a stack outside the u-area.
    for (i = 0; i < USIZE; i++) {
        if (i == USTKD) {
            if (up[i] != (unsigned)USIZE)
                return (15);
            continue;
        }
        if (up[i] != (unsigned)((i < 8 ? PATTERN1 : PATTERN2) ^ i))
            return (15);
    }

    // And the copy really went to physical UHOME, above 0100000 -- not to some page the two
    // routines happen to share.  Map the home page at virtual page 0 and read it back.  Word 5,
    // not word 0: virtual address 0 is the black hole.
    tx.x_caddr = UHOME;
    sureg();
    if (peek(5) != (unsigned)(PATTERN1 ^ 5))
        return (16);

    // Task 11: copyseg()/clearseg() (kernel/seg.S), reaching a page above 0100000.
    //
    // SEGSRC and SEGDST are two pool pages out of reach of any unmapped access -- the whole
    // reason the two routines have to window them.  Fill the live page at UBASE (which the
    // kernel, and we, reach unmapped) with a source pattern and DO NOT drain: copyseg's own
    // leading drain has to settle those unmapped, physical-tagged stores before it reads the
    // page back mapped, or it copies stale memory.  That is the hazard this leg exists for.
    up = (volatile unsigned *)UBASE;
    for (i = 0; i < PGSZ; i++)
        up[i] = PATTERN1 ^ i;

    copyseg(UBASE, SEGSRC * PGSZ);         // low -> high: settles the fill, windows both pages
    copyseg(SEGSRC * PGSZ, SEGDST * PGSZ); // high -> high: a page above 0100000 to another

    // Read SEGDST back.  Map it at virtual page 0 and peek sample words -- not word 0, the
    // black hole.  copyseg's trailing drain is what put its mapped stores into physical memory;
    // without it this reads stale.
    tx.x_caddr = SEGDST * PGSZ;
    sureg();
    if (peek(5) != (unsigned)(PATTERN1 ^ 5))
        return (18);
    if (peek(500) != (unsigned)(PATTERN1 ^ 500))
        return (18);
    if (peek(PGSZ - 1) != (unsigned)(PATTERN1 ^ (PGSZ - 1)))
        return (18);

    // clearseg() zeroes the same page.
    clearseg(SEGDST * PGSZ);
    tx.x_caddr = SEGDST * PGSZ;
    sureg();
    if (peek(5) != 0)
        return (19);
    if (peek(PGSZ - 1) != 0)
        return (19);

    // Task 27: copyphys() -- the same bracket, opened at an OFFSET into each page and
    // counted in words.  It is what /dev/mem is read and written through (kernel/dev/mem.c),
    // so what has to be shown is that a PARTIAL run lands where it was asked to and nowhere
    // else: the words on either side of it must survive.
    //
    // SEGSRC still holds PATTERN1 ^ i (only SEGDST was cleared above), and UBASE is the low
    // page we can read back unmapped.  Restore the part of it check 18 left alone first --
    // clearseg and the copies above did not touch it, but say so rather than assume it.
    up = (volatile unsigned *)UBASE;
    for (i = 0; i < PGSZ; i++)
        up[i] = PATTERN1 ^ i;

    // High -> low: 50 words from the middle of a page above 0100000 into the middle of the
    // live u-area page.  That an ORDINARY UNMAPPED LOAD sees the result is the trailing
    // drain: the copy's stores were made mapped, under a virtual tag.
    copyphys(SEGSRC * PGSZ + 100, UBASE + 200, 50);
    for (i = 0; i < 50; i++)
        if (up[200 + i] != (unsigned)(PATTERN1 ^ (100 + i)))
            return (26);
    if (up[199] != (unsigned)(PATTERN1 ^ 199) || up[250] != (unsigned)(PATTERN1 ^ 250))
        return (26); // the run overran its ends

    // Low -> high, with a different pattern so that a copy landing in the wrong place cannot
    // pass by holding what was already there.  Then the two edge cases the driver's chopping
    // is there to produce: a run that ENDS on the last word of a page, and one that STARTS
    // at word 0 of one.
    for (i = 0; i < 40; i++)
        up[300 + i] = PATTERN3 ^ i;
    copyphys(UBASE + 300, SEGDST * PGSZ + 700, 40);
    copyphys(UBASE + PGSZ - 3, SEGDST * PGSZ + PGSZ - 3, 3);
    copyphys(UBASE, SEGDST * PGSZ, 4);

    // Word 0 of SEGDST cannot be peeked -- virtual address 0 is the black hole -- so bring it
    // back the other way, one word, and read THAT unmapped.
    copyphys(SEGDST * PGSZ, UBASE + 400, 1);
    if (up[400] != (unsigned)PATTERN1)
        return (28);

    // Everything else about SEGDST, read through the map at virtual page 0.
    tx.x_caddr = SEGDST * PGSZ;
    sureg();
    for (i = 0; i < 40; i++)
        if (peek(700 + i) != (unsigned)(PATTERN3 ^ i))
            return (27);
    if (peek(699) != 0 || peek(740) != 0)
        return (27); // clearseg's zeros on either side of the run
    for (i = 1; i <= 3; i++)
        if (peek(PGSZ - i) != (unsigned)(PATTERN1 ^ (PGSZ - i)))
            return (28);
    if (peek(PGSZ - 4) != 0)
        return (28);
    for (i = 1; i <= 3; i++)
        if (peek(i) != (unsigned)(PATTERN1 ^ i))
            return (28);
    if (peek(4) != 0)
        return (28);

    // Put the map back, so the .ini's РП/РЗ assertions describe the state it expects.
    tx.x_caddr = TEXTPG * PGSZ;
    sureg();

    // Task 12: copyin/copyout and the fu/su family (kernel/usermem.S).  The map above has the
    // data pages (physical 17-18) at virtual pages 2-3, so a word at 2*PGSZ+n is a real user
    // address -- reachable only through the map, which is exactly what these routines cross.

    // copyout: kernel buffer -> user page, read back through the map.
    for (i = 0; i < 4; i++)
        kbuf[i] = PATTERN2 ^ (i + 1);
    uw = 2 * PGSZ + 16;
    if (copyout((caddr_t)kbuf, (caddr_t)uw, 4 * NBPW) != 0)
        return (20);
    drainbrz();
    for (i = 0; i < 4; i++)
        if (peek(uw + i) != (unsigned)(PATTERN2 ^ (i + 1)))
            return (20);

    // copyin: user page -> kernel buffer.
    uw = 2 * PGSZ + 24;
    for (i = 0; i < 4; i++)
        poke(uw + i, PATTERN1 ^ (i + 1));
    drainbrz();
    for (i = 0; i < 4; i++)
        kbuf[i] = 0;
    if (copyin((caddr_t)uw, (caddr_t)kbuf, 4 * NBPW) != 0)
        return (21);
    for (i = 0; i < 4; i++)
        if (kbuf[i] != (unsigned)(PATTERN1 ^ (i + 1)))
            return (21);

    // fuword / suword round trip.
    uw = 2 * PGSZ + 40;
    if (suword((caddr_t)uw, PATTERN3) != 0)
        return (22);
    if ((unsigned)fuword((caddr_t)uw) != PATTERN3)
        return (22);

    // subyte / fubyte at every byte offset.  Each subyte is a read-modify-write that must set
    // its byte and preserve the other five; after all six the packed word must read back exact.
    uw = 2 * PGSZ + 48;
    poke(uw, 0);
    drainbrz();
    for (i = 0; i < 6; i++)
        if (subyte(UBYTE(uw, i), 0300 | i) != 0)
            return (23);
    drainbrz();
    // byte #k occupies bits (41-8k)..(48-8k) of the word: (w >> (8*(5-k))) & 0377.
    w = peek(uw);
    for (i = 0; i < 6; i++)
        if (((w >> (8 * (5 - i))) & 0377) != (unsigned)(0300 | i))
            return (23);
    for (i = 0; i < 6; i++)
        if (fubyte(UBYTE(uw, i)) != (int)(0300 | i))
            return (23);

    // The exec argument vector's contract (exece(), kernel/sys1.c).  exece() lays each
    // string down one subyte() at a time through a walking char *, and stores that same
    // char * into the block as argv[i]; the program then dereferences it directly.  Nothing
    // can run exec until a root filesystem exists, so round-trip the contract here instead:
    // the marker and the byte offset must survive suword()/fuword(), and the walk must carry
    // across the word boundary the string deliberately straddles.
    uw = 2 * PGSZ + 56;            // the argv[0] slot
    cq = (caddr_t)(int *)(uw + 1); // the string, in the words just above it
    if (suword((caddr_t)uw, (int)cq) != 0)
        return (25);
    for (i = 0; argstr[i] != 0; i++)
        if (subyte(cq++, argstr[i]) != 0)
            return (25);
    // what came back must be a FAT pointer: byte #0 (offset 5) of the string's first word
    cq = (char *)fuword((caddr_t)uw);
    if (ptrword(cq) != (int)(uw + 1) || ptrbyte(cq) != 5)
        return (25);
    for (i = 0; argstr[i] != 0; i++)
        if (fubyte(cq++) != argstr[i])
            return (25);

    // An address in an unmapped page (virtual page 10, the hole of check 5) must be rejected with
    // -1 and never touch memory -- useracc() sees the zero descriptor, so there is no fault path.
    uw = 10 * PGSZ;
    if (fuword((caddr_t)uw) != -1)
        return (24);
    if (suword((caddr_t)uw, 0) != -1)
        return (24);
    if (fubyte((caddr_t)uw) != -1)
        return (24);
    if (subyte((caddr_t)uw, 0) != -1)
        return (24);
    if (copyin((caddr_t)uw, (caddr_t)kbuf, 2 * NBPW) == 0)
        return (24);
    if (copyout((caddr_t)kbuf, (caddr_t)uw, 2 * NBPW) == 0)
        return (24);

    return (0);
}
