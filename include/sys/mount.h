/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * Mount structure.
 * One allocated on every mount.
 * Used to find the super block.
 */
struct mount {
    dev_t m_dev;           /* device mounted */
    struct buf *m_bufp;    /* pointer to superblock */
    struct inode *m_inodp; /* pointer to mounted on inode */
};

extern struct mount mount[NMOUNT];
