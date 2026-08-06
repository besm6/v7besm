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
// BOTH FUNCTIONS HAVE THE SAME THREE-PART SHAPE: peel bytes until the DESTINATION stands on a
// word boundary, move the whole destination words in the middle, peel the tail.  Only the
// middle differs, and it differs on the phase:
//
//   IN PHASE, source byte k lands on destination byte k for the whole run, so the middle is a
//   straight word copy -- copyin/copyout, which is where the mode-toggle bracket is amortised
//   over a whole run rather than paid per word.
//
//   OUT OF PHASE, every word of the transfer straddles two on the other side, and the middle
//   is a FUNNEL SHIFT: destination word i is the tail of source word i joined to the head of
//   source word i+1.  That wants a 96-bit shift, which this machine has (asx/asn shift [A, Y]
//   as one quantity) and this kernel does not use -- but it does not need it, because
//   `unsigned' here is exactly one 48-bit word and two shifts and an `or' say the same thing
//   in C.  So the out-of-phase middle also moves a word at a time, one boundary crossing per
//   six bytes instead of six, and no assembly.
//
// Either way only the two ends are byte-at-a-time, and they are at most five bytes each.
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
// AND IT IS WHY THE FUNNEL DOES NOT TEST fuword().  fuword() reports a bad address as -1, which
// for a WORD is in band -- a user word may legitimately hold that bit pattern -- so the test
// would be wrong as often as it was right.  It is also unnecessary: the whole range is already
// known mapped.  suword() returns 0 or -1 and nothing else, so that one is still tested.
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
// exactly like one that is.  vmstat(8) prints all three, through kctl(2).  The split between
// them is what said the two bulk arms were worth building at all -- see iomove()'s comment in
// rdwri.c for the numbers.  Plain ints, no spl bracket: a lost count is not a bug worth one.
int niobulk;  // bytes moved by copyin/copyout: whole words, both pointers on byte #0
int nioedge;  // bytes moved one at a time to square up the two ends of a transfer
int nioshift; // bytes moved a word at a time through the funnel: the phases DIFFER

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
    register unsigned *kp;
    unsigned cur, nxt;
    caddr_t up;
    int k, sh, rsh, left;

    if (n <= 0)
        return (0);
    if (!uspan(from, n, 0))
        return (-1);

    // Square the DESTINATION up to a word boundary.  ptrbyte() == 5 already IS byte #0, and
    // the test for it is a pure optimisation -- without it the whole first word would be
    // peeled one byte at a time and the result would still be right.  umem cannot see the
    // difference; the counters are what defend this line.
    lead = ptrbyte(to) + 1;
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

    // `to' now stands on byte #0 -- or n is 0 and nothing below runs.
    mid = n - n % NBPW;
    if (mid != 0) {
        if (ptrbyte(from) == ptrbyte(to)) {
            if (copyin(from, to, mid) < 0)
                return (-1);
            niobulk += mid;
        } else {
            // The funnel.  k is the source's byte index, 1..5 here: it cannot be 0, because
            // that is the destination's index and the phases differ -- so neither shift is by
            // zero or by a whole word, both of which would be undefined.
            k   = NBPW - 1 - ptrbyte(from);
            sh  = 8 * k;
            rsh = 8 * (NBPW - k);
            kp  = (unsigned *)ptrword(to);
            // fuword() masks the byte field away, so a pointer standing mid-word fetches the
            // word CONTAINING that byte, and + NBPW steps to the next one at the same index.
            // The word one past the last one stored is still inside the validated range:
            // destination word i needs source bytes 6i..6i+5, and the last of those is at
            // k + mid - 1, which is the last byte the transfer asked for.
            up  = from;
            cur = (unsigned)fuword(up);
            for (left = mid; left != 0; left -= NBPW) {
                up += NBPW;
                nxt = (unsigned)fuword(up);
                *kp = (cur << sh) | (nxt >> rsh);
                kp++;
                cur = nxt;
            }
            nioshift += mid;
        }
        from += mid;
        to += mid;
        n -= mid;
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
    register unsigned *kp;
    unsigned cur, nxt;
    caddr_t tp;
    int k, sh, rsh, left;

    if (n <= 0)
        return (0);
    if (!uspan(to, n, 1))
        return (-1);

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
        if (ptrbyte(from) == ptrbyte(to)) {
            if (copyout(from, to, mid) < 0)
                return (-1);
            niobulk += mid;
        } else {
            // The funnel, the other way round: the source is kernel memory and reads as whole
            // words, the destination is the user and takes one suword per six bytes.  k, the
            // shifts and the one-word lookahead are copyinb's, for the same reasons.
            k   = NBPW - 1 - ptrbyte(from);
            sh  = 8 * k;
            rsh = 8 * (NBPW - k);
            kp  = (unsigned *)ptrword(from);
            tp  = to;
            cur = *kp;
            for (left = mid; left != 0; left -= NBPW) {
                kp++;
                nxt = *kp;
                if (suword(tp, (int)((cur << sh) | (nxt >> rsh))) < 0)
                    return (-1);
                cur = nxt;
                tp += NBPW;
            }
            nioshift += mid;
        }
        from += mid;
        to += mid;
        n -= mid;
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
