// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// A directory entry: exactly four words -- one of i-number, three of name -- so
// DIRPB (128) of them tile a 512-word block and the directory offset arithmetic
// keeps its shifts.  v7's DIRSIZ of 14 made a 16-byte entry against a 512-byte
// block; this port had carried it to 24 chars, which made a FIVE-word entry, and 5
// does not divide 512.
//
// DIRSIZ lives in sys/param.h, which is where sys/user.h reads it for u_dbuf and
// u_comm.  It is not defaulted here: one home only.

#ifndef _SYS_DIR_H
#define _SYS_DIR_H

struct direct {
    ino_t d_ino;
    char d_name[DIRSIZ];
};

// The layout is the on-disk format; see the same assertions in sys/ino.h.
_Static_assert(sizeof(struct direct) == DIRENTSZ, "struct direct must be DIRWORDS words");
_Static_assert(sizeof(struct direct) * DIRPB == BSIZE, "DIRPB entries must tile a block");

#endif // _SYS_DIR_H
