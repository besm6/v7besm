/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 1999 Robert Nordier. All rights reserved. */

//
// The shell's storage allocator -- circular first fit, S. R. Bourne's original.
//
// It is the same algorithm as lib/libc/gen/malloc.c, which is v7's C allocator ported
// to this machine; THAT FILE'S HEADER COMMENT IS THE REFERENCE for why the busy flag
// sits where it does, and it is not repeated here.  The short of it: a block is exactly
// one word and an address is a word index, so adjacent blocks differ by 1 and bit 0 is
// a significant address bit.  Setting it would name the next block.  The flag moves
// above the address instead, to bit 16 -- free, because a regular (non-fat) pointer
// carries its 15-bit word address in bits 15-1 with everything above it zero, and far
// below the bit-48 marker that would make a tagged link look like a fat char *.
//
// v7 called these alloc() and free() and then wrote `#define alloc malloc' in defs.h,
// so this file DEFINED malloc and free for the whole program.  Here that would collide
// the moment anything pulled libc's malloc.o in -- stdio, calloc and exit all reference
// it -- so the shell's arena keeps its own names and libc's allocator stays unused.
//
// The arena is fused to the expression stack (stak.c): the stack lives immediately
// above bloktop, and addblok() is what moves it when the arena grows underneath.  That
// coupling is why this allocator survives at all rather than being replaced by libc's.
//
// v7's `#ifdef DEBUG chkbptr' is gone rather than carried compiled out: it reports
// through prn()/prc() and calls abort(), and it walked the arena from `end' as a BLKPTR.
//
#include "defs.h"

//
// Bit 16, one past the largest word address.  See lib/libc/gen/malloc.c.
//
#define BUSY 0100000

#define testbusy(p)  ((POS)(p) & BUSY)
#define setbusy(p)   ((BLKPTR)((POS)(p) | BUSY))
#define clearbusy(p) ((BLKPTR)((POS)(p) & ~(POS)BUSY))
#define busy(x)      testbusy((x)->word)

POS brkincr = BRKINCR;

BLKPTR blokp;   // current search pointer
BLKPTR bloktop; // top of arena (last blok); seeded by the first addblok()

ADDRESS shalloc(POS nbytes)
{
    // Header word plus payload, rounded up to whole words.  v7 wrote
    // round(nbytes+BYTESPERWORD, BYTESPERWORD), whose bit mask needs a power-of-two
    // modulus; here the modulus is 6.  Signed, so that the size comparison below is a
    // signed one -- v7's POS made it unsigned, and an arena walked backwards would have
    // read a negative span as an enormous free block.
    INT rbytes = sizeup(nbytes + BYTESPERWORD);

    for (;;) {
        INT c    = 0;
        BLKPTR p = blokp;
        BLKPTR q;

        do {
            if (!busy(p)) {
                // Coalesce the run of idle blocks ahead of p.
                while (!busy(q = p->word))
                    p->word = q->word;
                if (ADR(q) - ADR(p) >= rbytes) {
                    blokp = BLK(ADR(p) + rbytes);
                    if (q > blokp)
                        blokp->word = p->word;
                    p->word = setbusy(blokp);
                    return ADR(p + 1);
                }
            }
            q = p;
            p = clearbusy(p->word);
        } while (p > q || (c++) == 0);

        addblok((POS)rbytes);
    }
}

//
// Grow the arena by at least `reqd' char-units and move the expression stack up on top
// of the new top block.
//
void addblok(POS reqd)
{
    STKPTR stakadr;

    //
    // Seed the arena on the first call.  v7 initialized bloktop with BLK(end) as a
    // static initializer; `end' is a char[] here, so its decay is a fat pointer and
    // that initializer would need a relocated fat-pointer constant.  main() calls
    // addblok(0) before anything can allocate, so the seed goes here instead.
    //
    if (bloktop == 0)
        bloktop = BLK(end);

    if (stakbas != staktop) {
        STKPTR rndstak;
        BLKPTR blokstak;

        // Hand the current stack item to the arena as a busy block, so that tdystak()
        // can give it back later.
        pushstak(0);
        rndstak        = wordup(staktop);
        blokstak       = BLK(stakbas) - 1;
        blokstak->word = stakbsy;
        stakbsy        = blokstak;
        bloktop->word  = setbusy(BLK(rndstak));
        bloktop        = BLK(rndstak);
    }

    //
    // Round the request up to a whole number of increments, always taking at least one
    // more.  That is what v7's `reqd += brkincr; reqd &= ~(brkincr-1)' came to while
    // the increment was a power of two; written as arithmetic because it no longer is,
    // and because stak.c grew the increment into non-powers of two even on the PDP-11.
    //
    reqd = (reqd / brkincr + 1) * brkincr;

    blokp = bloktop;

    //
    // v7 wrote this as BLK(Rcheat(bloktop)+reqd): a BYTE count added to the integer
    // value of a WORD pointer, which are the same unit only on a byte-addressed
    // machine.  `reqd' is a whole number of words by construction, so the arithmetic is
    // exact on a char * and the cast back lands on a word boundary.
    //
    bloktop = bloktop->word = BLK(ADR(bloktop) + reqd);

    //
    // The arena's end sentinel needs two properties: it must link BELOW bloktop, which
    // is how shalloc()'s `p > q' recognises the wrap, and it must read as busy so the
    // search never coalesces through it.  v7 got both from ADR(end)+1 -- one byte past
    // `end' is below bloktop and has bit 0 set.  Neither survives here: bit 0 is an
    // address bit, and the +1 would be flattened straight back onto `end' by the cast.
    // Said outright instead.
    //
    bloktop->word = setbusy(BLK(end));

    stakadr = STK(bloktop + 2);

    //
    // Cover the stack's new home before copying the current item into it.  The arena
    // has just grown into where the break was, so stakadr is above it; v7 left the
    // repair to the next locstak() and wrote its item into the slack a page-granular
    // break happened to leave.  This machine's break is page-granular too, but `end'
    // can sit anywhere in a page, so the slack is not always there.  The test is
    // locstak()'s own, applied at the moment the stack moves.
    //
    while (brkend - stakadr < BRKINCR) {
        if (!setbrk((INT)brkincr))
            error(nospace);
    }

    staktop = movstr(stakbot, stakadr);
    stakbas = stakbot = stakadr;
}

void shfree(BLKPTR ap)
{
    BLKPTR p = ap;

    //
    // Callers pass STRING as often as BLKPTR -- trapcom[i], namval, an argv element --
    // and the prototype converts.  That conversion floors a fat pointer to its word,
    // which is exactly right: shalloc() returned ADR(p+1), a fat pointer at byte #0 of
    // that word, so flooring recovers the block it came from.
    //
    if (p != 0 && p < bloktop) {
        --p;
        p->word = clearbusy(p->word);
    }
}
