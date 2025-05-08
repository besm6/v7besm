/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

struct map {
    short m_size;
    unsigned short m_addr;
};

extern struct map coremap[CMAPSIZ]; /* space for core allocation */
extern struct map swapmap[SMAPSIZ]; /* space for swap allocation */

#ifdef KERNEL
int malloc(struct map *mp, int size);
void mfree(struct map *mp, int size, int a);
#endif
