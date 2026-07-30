// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// A free-list chain block: the overflow store for the superblock's s_free[].
//
// The two arrays are THE SAME LENGTH by necessity -- alloc() and free() wcopy()
// between s_free[] and df_free[] sizing the copy from the filsys side
// (kernel/alloc.c), so a mismatch would silently overrun one of them.  Nothing had
// ever asserted that this block fits a block, either; NICFREE is now large enough
// that it is worth saying out loud.

#ifndef _SYS_FBLK_H
#define _SYS_FBLK_H

// sys/param.h and sys/types.h are INCLUDED, not assumed of the caller -- sys/dir.h's
// precedent, and for its reason: this file cannot compile without either, the struct
// naming NICFREE and daddr_t and the assertion below naming BSIZE.  Requiring an include
// order is requiring something no compiler checks.  It is also the only way that order
// survives clang-format, which sorts a block of <> includes alphabetically and so puts
// this header ahead of the two it depends on.
#include <sys/param.h>
#include <sys/types.h>

struct fblk {
    int df_nfree;
    daddr_t df_free[NICFREE];
};

_Static_assert(sizeof(struct fblk) <= BSIZE, "a free-list block must fit one block");

#endif // _SYS_FBLK_H
