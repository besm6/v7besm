// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// A directory entry: exactly four words -- one of i-number, three of name -- so
// DIRPB (128) of them tile a 512-word block and the directory offset arithmetic
// keeps its shifts.  v7's DIRSIZ of 14 made a 16-byte entry against a 512-byte
// block; this port had carried it to 24 chars, which made a FIVE-word entry, and 5
// does not divide 512.
//
// DIRSIZ lives in sys/param.h, which is where sys/user.h reads it for u_dbuf and
// u_comm.  It is not defaulted here: one home only -- but it IS included, along
// with sys/types.h for ino_t, rather than assumed of the caller.  This file
// cannot compile without either (the assertions below name BSIZE), and it sorts
// ahead of both in an include list clang-format has put in order.
//
// THAT IS THE RULE FOR THIS WHOLE DIRECTORY, not a habit of this one file: every
// header here includes what it uses, because requiring an include order is
// requiring something no compiler checks and no formatter preserves.  v7 required
// one -- types, param, systm, then the rest -- and the kernel's sources had to
// bracket their include blocks in `// clang-format off' to keep it.  They no longer
// do.  sys/param.h is the one header that includes nothing, and it earns that by
// emitting no C text at all; its head comment says so.

#ifndef _SYS_DIR_H
#define _SYS_DIR_H

#include <sys/param.h>
#include <sys/types.h>

struct direct {
    ino_t d_ino;
    char d_name[DIRSIZ];
};

// The layout is the on-disk format; see the same assertions in sys/ino.h.
_Static_assert(sizeof(struct direct) == DIRENTSZ, "struct direct must be DIRWORDS words");
_Static_assert(sizeof(struct direct) * DIRPB == BSIZE, "DIRPB entries must tile a block");

#endif // _SYS_DIR_H
