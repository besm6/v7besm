/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * Structure of the super-block.
 *
 * EXACTLY ONE BLOCK -- 512 words -- and the assertion at the foot is what keeps it
 * that way.  That is not tidiness: iinit() copies btow(sizeof(struct filsys)) words
 * into a fresh geteblk() buffer while update() writes BSIZEW words of that buffer
 * back to block 1, so anything short of a full block left the tail as uninitialised
 * buffer contents and put them on the disk at the first sync.  At 165 words -- what
 * v7's layout comes to here -- that was 347 words of stale kernel memory per sync.
 * Sized to the block, the two agree by construction.
 *
 * The v7 struct wasted those 347 words because NICINOD and NICFREE were chosen for
 * a 512-BYTE block.  See sys/param.h for how the space is now divided.
 *
 * s_flock/s_ilock/s_fmod/s_ronly are `int' here, not v7's `char'.  A char ARRAY
 * packs six to a word (sys/dir.h asserts it), but whether adjacent scalar char
 * members share a word is documented nowhere in doc/ -- and the size of this struct
 * is now load-bearing.  `int' removes the question instead of depending on the
 * answer, at a cost of three words in a block that has eleven spare.
 *
 * s_m/s_n/s_tfree/s_tinode are dead here -- v7 says so itself, and this port has no
 * ustat() -- but they stay, because mkfs and fsck will be ported from v7 sources and
 * four words is not worth the divergence.
 */
struct filsys {
    /* Identity and geometry: checked at mount by sbcheck(), kernel/alloc.c. */
    int s_magic; /*  0: FS_MAGIC */
    int s_bsize; /*  1: words per block (BSIZEW) */
    int s_inopb; /*  2: inodes per block (INOPB) */
    int s_naddr; /*  3: disk addresses per inode (NADDR) */

    int s_isize;     /*  4: size in blocks of i-list */
    daddr_t s_fsize; /*  5: size in blocks of entire volume */
    time_t s_time;   /*  6: last super block update */
    daddr_t s_tfree; /*  7: total free blocks */
    ino_t s_tinode;  /*  8: total free inodes */

    int s_flock; /*  9: lock during free list manipulation */
    int s_ilock; /* 10: lock during i-list manipulation */
    int s_fmod;  /* 11: super block modified flag */
    int s_ronly; /* 12: mounted read-only flag */

    int s_m;          /* 13: interleave factor */
    int s_n;          /* 14: " " */
    char s_fname[12]; /* 15..16: file system name */
    char s_fpack[12]; /* 17..18: file system pack name */

    int s_nfree;             /* 19: number of addresses in s_free */
    daddr_t s_free[NICFREE]; /* 20..339: free block list */
    int s_ninode;            /* 340: number of i-nodes in s_inode */
    ino_t s_inode[NICINOD];  /* 341..500: free i-node list */

    int s_fill[11]; /* 501..511: room to grow; written as zero */
};

/*
 * The layout IS the on-disk format, and the SIZE is load-bearing -- see above.
 * _Static_assert, not `extern int x[1 - 2*(cond)]': b6cc accepts a negative array
 * size without a word, so that idiom is decorative here.  Verified.
 */
_Static_assert(sizeof(struct filsys) == BSIZE, "the superblock must be exactly one block");
