// ucopy.c -- byte-granular copies to and from user memory.
//
//   int copyinb (caddr_t from, caddr_t to, int n);   // user  -> kernel
//   int copyoutb(caddr_t from, caddr_t to, int n);   // kernel -> user
//
// These are what iomove() (rdwri.c) calls.  copyin/copyout (usermem.S) are WORD-ONLY and
// deliberately stay that way -- their header says why -- so the byte offsets are peeled here,
// in C, and the assembly is handed only whole words standing on byte #0.
//
// WHAT A PHASE IS.  A caddr_t is a fat pointer: bit 48 marks it, bits 47-45 hold a byte offset
// as a RIGHT-SHIFT DISTANCE, bits 15-1 the word address (doc/Besm6_Data_Representation.md §7).
// ptrbyte() yields that shift field, in which 5 is the word's FIRST byte and 0 its last -- so
// the bytes left in the current word, counting the one under the pointer, are ptrbyte(p) + 1,
// and incrementing a char* DECREMENTS the field, borrowing into the word address on the wrap.
// Two pointers are IN PHASE when their fields are equal: they stand on the same byte of their
// respective words, so after the same number of steps they reach a word boundary together.
//
// WHY EQUAL PHASE IS THE TRACTABLE CASE, AND THE ONLY ONE HANDLED IN BULK.  A word copy moves
// six bytes at once and cannot shift them; it is correct only if source byte k lands on
// destination byte k.  In phase, the whole middle of a transfer is exactly that, and only the
// two ends are partial -- at most five bytes each.  Out of phase, EVERY word of the transfer
// straddles two words on the other side, which is a shifting copy across word boundaries and
// needs a machine-language funnel shift (asx/yta) that nothing in this kernel has.  So the
// out-of-phase case stays byte-at-a-time, and the counters below are what says how much of the
// traffic that is.
//
// THE RANGE IS VALIDATED UP FRONT, ONCE, AND THE SPAN IS A CEILING.  useracc() counts WORDS,
// and n bytes starting at byte k of a word touch (k + n + NBPW - 1) / NBPW of them -- a FLOOR
// would miss a trailing partial word sitting in the first unmapped page, which is precisely the
// case a copy that runs off the end of the user's data segment presents.  Validating here makes
// copyinb/copyoutb ALL-OR-NOTHING, which is what usermem.S already promises for copyin/copyout
// ("a range that runs into an unmapped page returns -1 without ever touching user memory") and
// what the old byte path did NOT: cpass/passc copied a prefix and then faulted.  Nothing
// observes the difference -- rdwr() discards r_val1 once u_error is set -- and one useracc()
// against up to 3072 bytes is noise.  The per-byte fubyte/subyte below revalidate their own
// single word; that is redundant, not wrong, and it is the price of not duplicating usermem.S.
//
// NOT REACHABLE FROM AN INTERRUPT HANDLER, because usermem.S's scratch cells are static and
// this calls into them.  Same restriction the routines it calls already carry.

#include "sys/dir.h"
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/types.h"
#include "sys/user.h"

// iomove() traffic, in BYTES, by the arm that carried it.  They exist to be OBSERVED, on the
// same argument as the swapper's counters in systm.h: a bulk path that is never taken looks
// exactly like one that is, and kernel/test/libtest asserts niobulk is non-zero for that
// reason.  The split between the two byte arms is what said this work was worth doing at all
// -- see iomove()'s comment in rdwri.c for the numbers.  Plain ints, no spl bracket: a lost
// count is not a bug worth one.
int niobulk;  // bytes moved by copyin/copyout: whole words, both pointers on byte #0
int nioedge;  // bytes moved one at a time to square up the two ends of an in-phase transfer
int nioshift; // bytes moved one at a time because the phases DIFFER -- what is left to do

// Is the user's [up, up+n) mapped?  A word count, rounded UP, from the pointer's byte
// position: byte k of a word is 5 - ptrbyte(p), and k + n bytes reach into
// (k + n + NBPW - 1) / NBPW words.  rw is passed for documentation; useracc() ignores it.
static int uspan(caddr_t up, int n, int rw)
{
    register int k;

    k = NBPW - 1 - ptrbyte(up);
    return (useracc(ptrword(up), (k + n + NBPW - 1) / NBPW, rw));
}

// user -> kernel.  `to' is an ordinary kernel address, so the destination side is a plain
// store; only the source crosses the boundary.
int copyinb(register caddr_t from, register caddr_t to, register int n)
{
    register int lead, mid, c;

    if (n <= 0)
        return (0);
    if (!uspan(from, n, 0))
        return (-1);

    if (ptrbyte(from) != ptrbyte(to)) {
        nioshift += n;
        while (n-- != 0) {
            if ((c = fubyte(from)) < 0)
                return (-1);
            *to = c;
            to++;
            from++;
        }
        return (0);
    }

    // Square up to a word boundary.  ptrbyte() == 5 already IS byte #0, and the test for it
    // is a pure optimisation -- without it the whole first word would be peeled one byte at a
    // time and the result would still be right.  umem cannot see the difference; the counters
    // are what defend this line.
    lead = ptrbyte(from) + 1;
    if (lead == NBPW)
        lead = 0;
    if (lead > n)
        lead = n; // the transfer ends inside the first word
    n -= lead;
    nioedge += lead;
    while (lead-- != 0) {
        if ((c = fubyte(from)) < 0)
            return (-1);
        *to = c;
        to++;
        from++;
    }

    // Both pointers now stand on byte #0 -- or n is 0 and nothing below runs.
    mid = n - n % NBPW;
    if (mid != 0) {
        if (copyin(from, to, mid) < 0)
            return (-1);
        from += mid;
        to += mid;
        n -= mid;
        niobulk += mid;
    }

    nioedge += n;
    while (n-- != 0) {
        if ((c = fubyte(from)) < 0)
            return (-1);
        *to = c;
        to++;
        from++;
    }
    return (0);
}

// kernel -> user.  The mirror: `from' is a plain load, `to' crosses the boundary.  Each edge
// byte is a subyte, which is a read-modify-write of the containing user word -- there is no
// sub-word store on this machine, so a partial word costs a read either way.
int copyoutb(register caddr_t from, register caddr_t to, register int n)
{
    register int lead, mid;

    if (n <= 0)
        return (0);
    if (!uspan(to, n, 1))
        return (-1);

    if (ptrbyte(from) != ptrbyte(to)) {
        nioshift += n;
        while (n-- != 0) {
            if (subyte(to, *from) < 0)
                return (-1);
            to++;
            from++;
        }
        return (0);
    }

    lead = ptrbyte(to) + 1;
    if (lead == NBPW)
        lead = 0;
    if (lead > n)
        lead = n;
    n -= lead;
    nioedge += lead;
    while (lead-- != 0) {
        if (subyte(to, *from) < 0)
            return (-1);
        to++;
        from++;
    }

    mid = n - n % NBPW;
    if (mid != 0) {
        if (copyout(from, to, mid) < 0)
            return (-1);
        from += mid;
        to += mid;
        n -= mid;
        niobulk += mid;
    }

    nioedge += n;
    while (n-- != 0) {
        if (subyte(to, *from) < 0)
            return (-1);
        to++;
        from++;
    }
    return (0);
}
