// umem -- copyinb()/copyoutb() (kernel/ucopy.c), the byte-granular copies between user and
// kernel memory, driven over every phase combination there is.
//
// A standalone SIMH program in the shape of mmutest: crt0.s brings the machine up and calls
// main(), and the real utab.o, ucopy.o, usermem.o and brz.o are linked against a hand-built
// process.  Everything runs in supervisor mode -- reset leaves РежЭ set -- so `mod' (002 рег)
// is legal and the kernel's address-space code can be exercised with no kernel under it.
//
// THE MAP IS mmutest's, deliberately unchanged:
//
//      virtual page   0   1 |  2   3 | 4 .. 27 | 28 | 29 30 31
//      physical page  20  21|  17  18|  --     | 19 |  --
//                     text  |  data  | unmapped| stk| unmapped
//
// WHAT THIS TEST IS ABOUT, and why a round trip would not have found it.  A copy that is one
// byte out of place still reads back correctly if the same copy is used to read it -- so the
// check here is never a round trip.  Every case computes the expected contents of the WHOLE
// destination window independently, from the byte indices, and compares all of it: the bytes
// inside the range and, just as importantly, the bytes OUTSIDE it, which a copy that ran long
// or started early would have trampled and which nothing else would notice.
//
// The two byte alphabets are disjoint on purpose -- source bytes are 0200..0377 and filler
// bytes 0..077 -- so a byte that moved cannot accidentally match the byte that should be
// there.  Within each alphabet the value varies with the byte index, so a copy displaced by
// one byte is visible everywhere, not just at the ends.
//
// COVERAGE
//   1. 6 x 6 x 14: user byte index x kernel byte index x length 0..13, both directions.
//      Lengths 0..13 straddle every boundary the algorithm has -- shorter than the first
//      word's remainder, exactly to a word boundary, and into a third word.
//   2. The same 36 phase pairs at n = 200, which is the only case where the whole-word middle
//      spans many words and where the out-of-phase byte loop runs past a word boundary.
//   3. A copy across a page boundary -- and across one whose two pages are NOT physically
//      adjacent, which check 4 below asserts before trusting the leg.  mmutest's data pages
//      (virtual 2 and 3) are physical 17 and 18, so a crossing placed there would pass even if
//      the map were ignored entirely.  The text/data boundary, virtual 1 -> 2, is physical
//      21 -> 17, and the leg reads both halves back at their PHYSICAL addresses as well.
//   4. The fault legs: a range wholly inside the hole, and -- the one that matters -- a range
//      that STARTS mapped and ends in it.  The word span useracc() is given must be a CEILING
//      or a trailing partial word in the first unmapped page is not seen, the validation
//      passes, and the mapped part is written before the tail faults.  So that leg asserts the
//      mapped start is UNTOUCHED, not merely that -1 came back.
//
// main() returns 0 on success.  A nonzero return is packed octal; umem.ini repeats the table:
//
//      0100000 + (ku << 12) + (kk << 9) + (n << 3) + which   the 6 x 6 x 14 matrix
//      0200000 + (ku << 12) + (kk << 9) +            which   the n = 200 runs
//      1 .. 9                                                the forge and the special legs
//
// with ku the user-side byte index, kk the kernel-side byte index, and `which':
//      1  copyoutb returned nonzero
//      2  the user window does not match the oracle after copyoutb
//      3  copyinb returned nonzero
//      4  the kernel buffer does not match the oracle after copyinb

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

// A char* into user virtual word `w': cast the word address to int* and then to char* -- the
// compiler makes it a fat pointer at byte #0 (the MSB byte), and `+ k' walks toward the LSB, byte
// by byte, exactly as exec/namei do (doc/Besm6_Data_Representation.md §7).  This is the real path
// fubyte()/subyte() take; building the fat pointer by hand from an int misses the marker bit.
#define UBYTE(w, k) ((caddr_t)(int *)(w) + (k))

// The longest run, in words, that any case seeds and checks: 200 bytes from byte #5 of the
// first word needs 35 words, and the matrix needs 6.
#define NWMAX 36

static unsigned ksrc[NWMAX];               // the kernel-side source
static unsigned kdst[NWMAX];               // the kernel-side destination
static unsigned char expect[NWMAX * NBPW]; // the oracle, one byte per element

// Byte i of the window, in each alphabet.  Disjoint, and varying with i.
#define SRCBYTE(i) (0200 | ((i) & 077))
#define FILBYTE(i) ((i) & 077)

// Word i built from six bytes of one alphabet.  Byte #0 is the MSB (bits 48-41), so the
// first byte shifts furthest left.
static unsigned srcword(int i)
{
    register unsigned w;
    register int j;

    w = 0;
    for (j = 0; j < NBPW; j++)
        w = (w << 8) | SRCBYTE(i * NBPW + j);
    return (w);
}

static unsigned filword(int i)
{
    register unsigned w;
    register int j;

    w = 0;
    for (j = 0; j < NBPW; j++)
        w = (w << 8) | FILBYTE(i * NBPW + j);
    return (w);
}

// Word i of the oracle, packed the same way -- for the page-crossing leg, which compares
// against physical memory and so needs whole words rather than bytes.
static unsigned expword(int i)
{
    register unsigned w;
    register int j;

    w = 0;
    for (j = 0; j < NBPW; j++)
        w = (w << 8) | expect[i * NBPW + j];
    return (w);
}

// Lay the oracle down: filler everywhere, then n source bytes from source index `si'
// spliced in at destination index `di'.
static void oracle(int nw, int di, int si, int n)
{
    register int i;

    for (i = 0; i < nw * NBPW; i++)
        expect[i] = FILBYTE(i);
    for (i = 0; i < n; i++)
        expect[di + i] = SRCBYTE(si + i);
}

// kernel -> user: ksrc byte kk to window byte ku, n bytes.  Returns 0, or a `which' code.
static int outcase(unsigned uw, int ku, int kk, int n, int nw)
{
    register int i, j;
    unsigned w;

    for (i = 0; i < nw; i++) {
        poke(uw + i, filword(i));
        ksrc[i] = srcword(i);
    }
    drainbrz();

    if (copyoutb(UBYTE(ksrc, kk), UBYTE(uw, ku), n) != 0)
        return (1);
    drainbrz();

    oracle(nw, ku, kk, n);
    for (i = 0; i < nw; i++) {
        w = peek(uw + i);
        for (j = 0; j < NBPW; j++)
            if (((w >> (8 * (NBPW - 1 - j))) & 0377) != expect[i * NBPW + j])
                return (2);
    }
    return (0);
}

// user -> kernel: window byte ku to kdst byte kk, n bytes.  Returns 0, or a `which' code.
static int incase(unsigned uw, int ku, int kk, int n, int nw)
{
    register int i, j;
    unsigned w;

    for (i = 0; i < nw; i++) {
        poke(uw + i, srcword(i));
        kdst[i] = filword(i);
    }
    drainbrz();

    if (copyinb(UBYTE(uw, ku), UBYTE(kdst, kk), n) != 0)
        return (3);

    oracle(nw, kk, ku, n);
    for (i = 0; i < nw; i++) {
        w = kdst[i];
        for (j = 0; j < NBPW; j++)
            if (((w >> (8 * (NBPW - 1 - j))) & 0377) != expect[i * NBPW + j])
                return (4);
    }
    return (0);
}

int main(void)
{
    unsigned uw, uwx;
    register int ku, kk, n, t;
    int i;

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

    // The forge is what the legs below assume, so assert it before trusting any of them.
    if (physaddr(PGSZ) != TEXTPG * PGSZ + PGSZ)
        return (1);
    if (physaddr(2 * PGSZ) != DATAPG * PGSZ)
        return (2);
    if (physaddr(4 * PGSZ) != 0) // the hole begins right after the data
        return (3);

    // AND ASSERT THE CROSSING IS A REAL ONE.  A page boundary between two pages that happen
    // to be physically adjacent proves nothing: the copy would come out right even if the
    // map were ignored.  This is why the crossing leg below is at the text/data boundary and
    // not inside the data, whose two pages ARE adjacent.
    if (physaddr(PGSZ) + PGSZ == physaddr(2 * PGSZ))
        return (4);

    // 1. Every phase pair against every short length.  The window sits well inside the data
    //    so that nothing here can wander into the crossing or fault legs' territory.
    uw = 2 * PGSZ + 64;
    for (ku = 0; ku < NBPW; ku++)
        for (kk = 0; kk < NBPW; kk++)
            for (n = 0; n <= 13; n++) {
                if ((t = outcase(uw, ku, kk, n, 6)) != 0)
                    return (0100000 + (ku << 12) + (kk << 9) + (n << 3) + t);
                if ((t = incase(uw, ku, kk, n, 6)) != 0)
                    return (0100000 + (ku << 12) + (kk << 9) + (n << 3) + t);
            }

    // 2. The same phase pairs at a length that makes the whole-word middle do real work --
    //    33 words of it, in phase -- and that runs the out-of-phase byte loop across many
    //    word boundaries.
    for (ku = 0; ku < NBPW; ku++)
        for (kk = 0; kk < NBPW; kk++) {
            if ((t = outcase(uw, ku, kk, 200, NWMAX)) != 0)
                return (0200000 + (ku << 12) + (kk << 9) + t);
            if ((t = incase(uw, ku, kk, 200, NWMAX)) != 0)
                return (0200000 + (ku << 12) + (kk << 9) + t);
        }

    // 3. Across the text/data boundary, in phase so that the bulk arm is the one that
    //    crosses it.  The window's word 4 is virtual 2*PGSZ, the first word on the far side.
    uwx = 2 * PGSZ - 4;
    if ((t = outcase(uwx, 3, 3, 40, 8)) != 0)
        return (5);
    // and read both halves back at their PHYSICAL addresses, which is the half that says the
    // mapping was honoured rather than the two pages being neighbours.
    drainbrz();
    if (*(volatile unsigned *)((TEXTPG + 1) * PGSZ + PGSZ - 1) != expword(3))
        return (6);
    if (*(volatile unsigned *)(DATAPG * PGSZ) != expword(4))
        return (6);
    if ((t = incase(uwx, 3, 3, 40, 8)) != 0)
        return (5);

    // 4a. A range wholly inside the hole: -1, and the kernel side untouched.
    uwx = 10 * PGSZ;
    for (i = 0; i < 2; i++) {
        ksrc[i] = srcword(i);
        kdst[i] = PATTERN2;
    }
    if (copyoutb(UBYTE(ksrc, 0), UBYTE(uwx, 0), 12) != -1)
        return (7);
    if (copyinb(UBYTE(uwx, 0), UBYTE(kdst, 0), 12) != -1)
        return (7);
    if (kdst[0] != PATTERN2 || kdst[1] != PATTERN2)
        return (7);

    // 4b. THE CEILING.  Seven bytes from byte #0 of the last mapped word span TWO words, and
    //     the second is in the hole.  A floor span says one word, useracc() approves it, and
    //     the first six bytes are written before the seventh faults -- so the assertion is
    //     that the mapped word still holds what it held, not merely that -1 came back.
    uwx = 4 * PGSZ - 1;
    poke(uwx, PATTERN1);
    drainbrz();
    kdst[0] = PATTERN2;
    if (copyoutb(UBYTE(ksrc, 0), UBYTE(uwx, 0), 7) != -1)
        return (8);
    drainbrz();
    if (peek(uwx) != PATTERN1)
        return (9);
    if (copyinb(UBYTE(uwx, 0), UBYTE(kdst, 0), 7) != -1)
        return (8);
    if (kdst[0] != PATTERN2)
        return (9);

    return (0);
}
