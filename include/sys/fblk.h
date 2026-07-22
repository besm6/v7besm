// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// A free-list chain block: the overflow store for the superblock's s_free[].
//
// The two arrays are THE SAME LENGTH by necessity -- alloc() and free() wcopy()
// between s_free[] and df_free[] sizing the copy from the filsys side
// (kernel/alloc.c), so a mismatch would silently overrun one of them.  Nothing had
// ever asserted that this block fits a block, either; NICFREE is now large enough
// that it is worth saying out loud.
struct fblk {
    int df_nfree;
    daddr_t df_free[NICFREE];
};

_Static_assert(sizeof(struct fblk) <= BSIZE, "a free-list block must fit one block");
