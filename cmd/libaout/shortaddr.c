
#include "besm6/b.out.h"

//
// The codec for a Format 1 (short) instruction's address field: a 12-bit offset
// in bits 12-1 plus the segment bit S in bit 19, worth 070000 of effective
// address. See the SHORTSEGBIT/SHORTSEG/SHORTOFF comment in <besm6/b.out.h> for
// the field layout and what it can reach.
//
// The assembler and the linker both patch such fields, and both must agree on
// the encoding, so the arithmetic lives here rather than in either of them.
// short_addr_get() and short_addr_put() round-trip every representable address,
// which is what lets a field survive being relocated once by the assembler and
// again by the linker.
//

// Decode the 15-bit effective address held in a short instruction's address field.
long short_addr_get(long insn)
{
    return (insn & SHORTOFF) | ((insn & SHORTSEGBIT) ? SHORTSEG : 0);
}

// True if address `a` is representable in a short address field. `a` is a plain
// address in the 15-bit space: a caller holding a negative literal or a wider
// expression reduces it modulo 0100000 first, the way the hardware forms EA.
// That reduction is what turns the -5 of "atx -5, 7" into 077773, an address the
// segment bit reaches; here anything outside the space is simply out of range.
int short_addr_fits(long a)
{
    if (a < 0 || a > 077777L)
        return 0;
    return a <= SHORTOFF || a >= SHORTSEG;
}

// Put address `a` into the short address field of `insn`, setting the segment
// bit iff `a` lies in the top eighth. Caller checks short_addr_fits() first: an
// unrepresentable `a` is masked here, not diagnosed.
long short_addr_put(long insn, long a)
{
    a &= 077777L;
    insn &= ~(SHORTOFF | SHORTSEGBIT);
    insn |= a & SHORTOFF;
    if (a >= SHORTSEG)
        insn |= SHORTSEGBIT;
    return insn;
}
