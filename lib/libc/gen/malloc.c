// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// The C storage allocator: v7's, algorithm for algorithm.
//
// circular first-fit strategy
// works with noncontiguous, but monotonically linked, arena
// each block is preceded by a ptr to the (pointer of) the next following block
// blocks are exact number of words long
// pointers to blocks must have BUSY bit 0
// bit in ptr is 1 for busy, 0 for idle
// gaps in arena are merely noted as busy blocks
// last block of arena is empty and has a pointer to first
// idle blocks are coalesced during space search
//
// Not the c-compiler's allocator (libc/besm6/malloc.c), which claims the whole span
// from `end' to the stack at 070000 on first use: that would defeat the kernel's
// demand-grown break and leave nothing between the heap and the stack.  This one grows
// through sbrk() and never gives anything back, exactly as v7 does.
//
// WHERE THE BUSY BIT LIVES.  This is the one change to the algorithm, and it is forced.
// v7 packs the flag into bit 0 of the link, which is free on a byte-addressed machine
// because `union store' is two bytes wide there and every block is even.  Here a block
// is exactly ONE WORD and an address is a WORD INDEX, so adjacent blocks differ by 1
// and bit 0 is a significant address bit -- setting it would name the next block.  The
// flag moves ABOVE the address instead: a regular (non-fat) pointer carries its 15-bit
// word address in bits 15-1 with bits 48-16 zero (doc/Besm6_Data_Representation.md
// §7), so bit 16 is free, and it sits far below the bit-48 marker that would make a
// marked link look like a fat `char *'.  Nothing else in the algorithm notices: v7
// already clears the flag before every dereference and every comparison.
//
// The casts the move takes -- pointer to integer and back -- are free here.  Every type is
// one word, so the compiler lowers both directions as a bare copy (emit_cast() in the
// c-compiler's translator/translate.c), and `(char *)p' on a word pointer sets the fat
// marker with offset #0, i.e. the FIRST byte of the word.  So the pointer handed to the
// caller addresses the first byte of its block, and free()'s cast back drops the offset
// and recovers the same word.
//
// WORDS, NOT BYTES.  WORD is sizeof(union store) == 6 char-units == one word, so every
// block returned starts on a word boundary by construction and there is nothing for
// v7's ALIGN/NALIGN to do.  The heap grows a PAGE at a time because that is the unit
// the break is granted in: both the kernel (sbreak(), kernel/sys1.c) and b6sim
// (SYS_break, cmd/sim/syscall.cpp) round the requested break UP to 1024 words, so a
// chunk smaller than a page buys nothing and a retry that backs off by less than a page
// asks for exactly what was just refused.
//
// v7's `#ifdef debug' scaffolding -- ASSERT(), botch() and allock() -- is gone rather than
// carried compiled out: it reports through printf(), which is phase 4, and abort(), which
// cannot say anything until signals are delivered (phase 6).  Its one structural trace is
// ialloc()'s search, where two nested ifs whose else branch was an ASSERT collapse into the
// single `s > r && p < q'.
//
// TWO C11 GUARDS ON TOP OF V7, since <stdlib.h> here is C11 and v7's was not:
// free(NULL) is a no-op (§7.22.3.3) and realloc(NULL, n) is malloc(n) (§7.22.3.5).
// v7 would fault on the first and corrupt the arena on the second.
//
#include <stdlib.h>

//
// sbrk() is declared by no header -- v7 has no <unistd.h> -- so it is declared where it
// is used, as lib/README.md's ground rules require.  IT FAILS WITH NULL, not with
// (char *)-1: v7's value would have to be fabricated from an integer, and a fat pointer
// cannot be (lib/libc/sys/sbrk.c).
//
char *sbrk(int incr);

// v7's donation entry point.  No header declares it there either.
void ialloc(char *qq, size_t nbytes);

//
// One word.  The union is v7's; on this machine it has nothing to reconcile, since
// every scalar is one word and a word is the coarsest alignment the machine has.
//
union store {
    union store *ptr;
    int align;
};

#define WORD  sizeof(union store) // char-units per block header: 6, one word
#define PAGE  1024                // words the break is granted in
#define BLOCK PAGE                // words asked of sbrk at once
#define FRAG  PAGE                // ... and the step a refused request backs off by

//
// Bit 16, one past the largest word address.  NWORDS is that same number read as the
// size of the address space, which is why the two constants coincide.
//
#define BUSY   0100000
#define NWORDS 0100000

#define testbusy(p)  ((unsigned)(p) & BUSY)
#define setbusy(p)   ((union store *)((unsigned)(p) | BUSY))
#define clearbusy(p) ((union store *)((unsigned)(p) & ~(unsigned)BUSY))

//
// `alloca' is v7's name for the arena's sentinel block -- not the alloca() of other
// systems, which this compiler does not have.  It is a `union store' of its own so that
// the empty arena still has a block to point at; ialloc() links it into a ring on the
// first insertion, and until then its null ptr is what says the arena is empty.
//
static union store alloca;            // initial arena
static union store *allocb = &alloca; // arena base
static union store *allocp = &alloca; // search ptr
static union store *allocx;           // for benefit of realloc

void *malloc(size_t nbytes)
{
    union store *p, *q;
    unsigned nw, temp;

    //
    // Refuse up front what the address space cannot hold.  A size_t is 48 bits wide and
    // a word address is 15, so without this the word count below would WRAP for a large
    // request -- nbytes of 2^48-1 rounds to one word -- and the allocator would happily
    // hand back a block a thousand times smaller than the caller asked for.
    //
    if (nbytes >= (size_t)NWORDS * WORD)
        return NULL;

    nw = (nbytes + WORD + WORD - 1) / WORD; // header plus payload, in words
    for (;;) {                              // done at most twice
        p = allocp;
        if (alloca.ptr != 0)
            for (temp = 0;;) {
                if (!testbusy(p->ptr)) {
                    while (!testbusy((q = p->ptr)->ptr)) {
                        p->ptr = q->ptr;
                    }
                    if (q >= p + nw && p + nw >= p)
                        goto found;
                }
                q = p;
                p = clearbusy(p->ptr);
                if (p <= q) {
                    if (p != allocb)
                        return NULL;
                    if (++temp > 1)
                        break;
                }
            }

        // Nothing in the arena fits: grow it, rounded up to whole pages.
        temp = ((nw + BLOCK) / BLOCK) * BLOCK;
        p    = (union store *)sbrk(0);
        if (p + temp <= p)
            return NULL;
        for (;;) {
            q = (union store *)sbrk((int)(temp * WORD));
            if (q != NULL)
                break;
            temp -= FRAG;
            if (temp <= nw)
                return NULL;
        }
        ialloc((char *)q, temp * WORD);
        allocp = allocb;
    }
found:
    allocp = p + nw;
    if (q > allocp) {
        allocx      = allocp->ptr;
        allocp->ptr = p->ptr;
    }
    p->ptr = setbusy(allocp);

    //
    // The payload begins one word past the header.  malloc(0) therefore costs one word
    // and hands back the address of the NEXT block's header: a zero-size payload, which
    // C11 permits so long as it is never dereferenced, and which free() undoes exactly.
    //
    return (char *)(p + 1);
}

//
// Freeing strategy tuned for LIFO allocation: the search pointer is left ON the block
// just released, so the next request of the same size gets it straight back.
//
void free(void *ap)
{
    union store *p;

    if (ap == NULL) // C11 §7.22.3.3: no action
        return;
    p      = (union store *)ap;
    allocp = --p;
    p->ptr = clearbusy(p->ptr);
}

//
// ialloc(q, nbytes) inserts a block that did not come from malloc into the arena.
//
// q points to new block
// r points to last of new block
// p points to last cell of arena before new block
// s points to first cell of arena after new block
//
void ialloc(char *qq, size_t nbytes)
{
    union store *p, *q, *s;
    union store *r;

    q      = (union store *)qq;
    r      = q + (nbytes / WORD) - 1;
    q->ptr = r;
    if (alloca.ptr == 0) // C can't initialize union
        alloca.ptr = &alloca;
    for (p = allocb;; p = s) {
        s = clearbusy(p->ptr);
        if (s == allocb)
            break;
        if (s > r && p < q)
            break;
    }
    p->ptr = q == p + 1 ? q : setbusy(q);
    r->ptr = s == r + 1 ? s : setbusy(s);
    if (allocb > q)
        allocb = q;
}

//
// realloc(p, nbytes) reallocates a block obtained from malloc() and freed since the
// last call of malloc(), to have new size nbytes and the old contents.  Returns the new
// location, or NULL on failure.
//
void *realloc(void *pp, size_t nbytes)
{
    union store *q;
    union store *p;
    union store *s, *t;
    unsigned nw;
    unsigned onw;

    if (pp == NULL) // C11 §7.22.3.5: equivalent to malloc(nbytes)
        return malloc(nbytes);

    p = (union store *)pp;
    if (testbusy(p[-1].ptr))
        free((char *)p);
    onw = p[-1].ptr - p;
    q   = (union store *)malloc(nbytes);

    //
    // v7 writes these two as one `return (char *)q'.  Kept apart because casting a null
    // WORD pointer to a fat one produces a pointer with the bit-48 marker and a byte
    // offset set over a zero address -- a null that is not a zero word.  `== NULL' sees
    // through that (the compiler compares the address part alone), but `if (!p)' need
    // not, so the failure path returns the real thing; and the in-place path hands back
    // the caller's own pointer rather than rebuilding one.
    //
    if (q == NULL)
        return NULL;
    if (q == p)
        return pp;

    s  = p;
    t  = q;
    nw = (nbytes + WORD - 1) / WORD;
    if (nw < onw)
        onw = nw;
    while (onw-- != 0)
        *t++ = *s++;
    if (q < p && q + nw >= p)
        (q + (q + nw - p))->ptr = allocx;
    return (char *)q;
}
