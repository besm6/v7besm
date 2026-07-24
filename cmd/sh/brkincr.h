/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// How much the shell's arena grows by at a time, in char-units.
//
// v7 used 512 and 4096 bytes, and blok.c rounded a request up to a multiple of the
// increment WITH A BIT MASK -- which needs a power of two, and which stak.c then broke
// anyway by growing the increment in steps of 256 (768, 1280, 1792: not powers of two).
// Neither the mask nor the power-of-two shape survives here, so the constants are
// chosen for what this machine actually does instead.
//
// THE BREAK IS GRANTED A PAGE AT A TIME.  Both the kernel (sbreak(), kernel/sys1.c) and
// b6sim (SYS_break, cmd/sim/syscall.cpp) round the requested break UP to 1024 words, so
// an increment smaller than a page buys nothing and a shrink of less than a page gives
// nothing back.  One page it is -- and, being a whole number of words, it also keeps
// every arena boundary word-aligned, which is what the casts in defs.h depend on.
//
#ifndef SH_BRKINCR_H
#define SH_BRKINCR_H

#include <sys/param.h>

#define BRKPAGE (1024 * NBPW) // 6144 char-units: one page of the break
#define BRKINCR BRKPAGE
#define BRKMAX  (4 * BRKPAGE)

#endif // SH_BRKINCR_H
