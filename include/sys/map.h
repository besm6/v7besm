// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// A resource map: coremap hands out words of physical core, swapmap hands out
// disk blocks (of BSIZE == 512 words).  A short is one 48-bit word, so both a
// 512 Kword address and a block count fit.
struct map {
    int m_size;
    int m_addr;
};

extern struct map coremap[CMAPSIZ]; // space for core allocation
extern struct map swapmap[SMAPSIZ]; // space for swap allocation

#ifdef KERNEL
int malloc(struct map *mp, int size);
void mfree(struct map *mp, int size, int a);
#endif
