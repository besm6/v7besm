// fstest -- the kernel reads a filesystem image that the build produced (task 19).
//
// The first time anything in this port has read a v7 filesystem with the kernel's own
// code.  cmd/fsutil builds the image, cmd/fsutil/test/kernel_model_test.cpp re-reads it
// with a transcription of kernel/alloc.c -- and until this test nothing closed the loop,
// so sbcheck() (kernel/alloc.c) had been compiled and never once executed.
//
// The path exercised end to end, all of it the real thing:
//
//      bread() -> getblk() -> bdevsw[0].d_strategy = mdstrategy() -> the controller
//              -> ГРП -> extintr() -> mdintr() -> iodone() -> iowait() -> sbcheck()
//
// So this sits strictly BELOW the boot path: everything iinit() does to mount a root,
// minus iinit() itself.  When the boot hang of task 20 is chased, the driver, the buffer
// cache and the superblock check are known good from here, and the suspect list is what
// booting adds over this program.
//
// WHAT IS LINKED AND WHAT IS FORGED.  bio.o, md.o, alloc.o, intr.o, utab.o and brz.o come
// from next door unchanged; this file supplies only the environment they name and that
// besm6.o/main.o/conf.o/prf.o/subr.o/iget.o would otherwise provide -- the buffer cache,
// one row of bdevsw[], and a handful of stubs.  Two of those stubs carry weight:
//
//   - sleep() calls extintr().  Delivery is blocked inside iowait() (the real spl6()), so
//     nothing else would ever set B_DONE and the wait would be a hang; polling the handler
//     from the sleep stub is what mdtest does in its xfer() loop, for the same reason.
//     Everywhere else the completion arrives through crt0.s's 0501 gate for real, because
//     bread() calls the strategy routine at spl0.
//   - panic() cannot return, so it leaves a marker in word PANICWORD and spins.  The .ini's
//     `step' turns that into a failure and prints the marker.
//
// THE BUFFER CACHE IS AT A FIXED PHYSICAL PAGE, exactly as it is in the kernel, where
// `buffers = BUFBASE' is an absolute symbol rather than bss (kernel/besm6.S).  It has to
// be: mdstrategy() refuses a transfer whose memory side is not half-zone grained, and
// bufpaddr() of a cache buffer is just the word address of b_addr.  Ordinary bss
// would land wherever the linker put it and be refused.  BUFPAGE also has to stay below
// word 32767, since a caddr_t's word field is 15 bits (ptrword(), sys/param.h).
//
// READ struct buf's block ONLY through a cast off b_addr.  This is what the test found the
// first time it ran, and it was a kernel bug, not a test one -- the sites were all over
// main.c, alloc.c, iget.c, sys3.c, subr.c and nami.c, and none had ever executed (task 20).
// b_addr is a caddr_t, a FAT pointer: marker in bit 48, byte offset in bits 47-45.  Assigning
// it to a plain word pointer keeps those bits, and the compiler adds a field's offset to it
// with `a+x' -- the ADDITIVE unit, which reads bits 48-42 as an exponent.  Adding 1 to a value
// with bit 48 set is a floating-point add of a number too small to matter: the result is the
// pointer, unchanged.  So the misread `fp->s_bsize' silently returned s_magic, every field
// past offset 0 read offset 0, and nothing faulted.
//
//      fp = (struct dinode *)bp->b_addr;   // BROKEN if bp came from a plain word pointer;
//      fp = (struct filsys *)bp->b_addr;   // right: the cast off b_addr strips the marker
//
// The cast is one `aax' against a mask, and it is what every line below does.  v7's named
// union members (b_filsys/b_dino/…) that invited the wrong spelling are gone (sys/buf.h).
//
// WHAT WOULD NOTICE IF THE CODE WERE WRONG -- the checks that exist for that reason alone:
//
//   - Check 7 reads block 0, which is NOT a superblock, and requires sbcheck() to reject
//     it and to have said so through prdev().  Without it a sbcheck() that returned 0
//     unconditionally would pass every other check in this file.
//   - Check 6 scribbles a sentinel over the in-core superblock before releasing it and
//     requires the sentinel to survive the next bread().  A re-read from the device would
//     quietly restore FS_MAGIC and look like a cache hit; only the sentinel tells the two
//     apart.
//   - Check 4 asserts s_time against the -T stamp test/CMakeLists.txt passes b6fsutil.  A
//     stale image left in the build tree, or one attached from somewhere else, fails here
//     rather than passing on the strength of being a filesystem at all.
//
// fstest.ini asserts ACC == 0.  A nonzero ACC names the failing check -- see the F_* bits.

// clang-format off
#include "sys/types.h"
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/dir.h"
#include "sys/user.h"
#include "sys/buf.h"
#include "sys/conf.h"
#include "sys/mount.h"
#include "sys/inode.h"
#include "sys/filsys.h"
#include "sys/ino.h"
#include "sys/stat.h"
// clang-format on

// The code under test.
struct buf *bread(dev_t dev, daddr_t blkno);
void brelse(struct buf *bp);
int incore(dev_t dev, daddr_t blkno);
int sbcheck(struct filsys *fp, dev_t dev);
void mdopen(dev_t dev, int flag);
void mdstrategy(struct buf *bp);
extern struct buf mdtab;
void extintr(void);
void intrinit(void);

// The image test/CMakeLists.txt builds, and the two numbers it is built with.  KEEP THESE
// IN STEP WITH THAT FILE: ROOTTIME is its -T stamp and ROOTBLKS its -s size.  The pairing
// is deliberate and is the whole of check 4 -- see the header.
#define ROOTTIME 1000000000
#define ROOTBLKS 2000

// Where the buffer cache lives: physical page 021, word 042000.  Page-aligned, so buffer i
// starts on a page or a half-page boundary and DISK_HALFPAGE can express it; clear of this
// image; and below the 32767 a caddr_t can name.  NBUF * BSIZEW = 5120 words, so it ends
// at 052000, well short of that ceiling.
#define BUFPAGE  021
#define BUFWORD  (BUFPAGE * PGSZ)

// A word in the const-segment hole between the service-word buffers (010-067) and the
// interrupt vectors at 0500: nothing in the image occupies it, so panic() can drop a
// marker there for the .ini to print.  mdtest uses 0100 for its mode word the same way.
#define PANICWORD (*(volatile int *)0101)
#define PANICMARK 0654321

// The sentinel check 6 leaves in the in-core superblock.  Nothing on the disk looks like
// it, which is the point.
#define CACHEPAT 0525252525252

// Fault-mask bits, returned in the accumulator.  Zero means every check passed.
#define F_RDERR 0000001 // a bread() that should have worked reported B_ERROR
#define F_SBCHK 0000002 // sbcheck() rejected the image's superblock
#define F_GEOM  0000004 // the geometry is not the one this kernel was built for
#define F_TIME  0000010 // s_time is not the stamp the build stamped the image with
#define F_FREE  0000020 // the free counts are out of range or empty
#define F_NOISY 0000040 // prdev() complained about a superblock that is good
#define F_CACHE 0000100 // a released block was not handed back from the cache
#define F_BITE  0000200 // sbcheck() accepted a block that is not a superblock
#define F_QUIET 0000400 // ...or did not report the one it rejected
#define F_ROOTI 0001000 // the root inode is not a directory with children
#define F_PANIC 0002000 // panic() ran (unreachable: it spins -- the .ini catches it)

// -------------------------------------------------------------------------
// The environment bio.o, md.o, alloc.o, intr.o and utab.o name.
// -------------------------------------------------------------------------

// In the kernel `u' is the absolute page at 076000 and maxmem is counted by startup();
// here they are just storage.  Nothing below maps anything, so bss will do.
struct user u;
int maxmem = 512 * 1024;
time_t time;

// getfs()/update() walk this; nothing here mounts anything, so it stays empty.
struct mount mount[NMOUNT];

// One block device: the disk, at major 0, which is what rootdev names (kernel/conf.c).
// d_close is never reached, and nblkdev is set here rather than counted, main.c's binit()
// being the thing that counts it.
struct bdevsw bdevsw[] = {
    { mdopen, 0, mdstrategy, &mdtab },
    {},
};
int nblkdev = 1;

struct buf buf[NBUF];
struct buf bfreelist;

// The root disk: major 0 minor 0, the same number kernel/conf.c gives the kernel and the
// same one the .ini's `attach md00' names.  root.manifest's /dev/md0 is this device.
dev_t rootdev = makedev(0, 0);

// bio.o's swap() names these.  Nothing here swaps.
dev_t swapdev = 0;
daddr_t swplo = 0;

int *intrframe; // extintr() dereferences it only on the timer arm

struct trap;

static int nclock; // free-running timer ticks; any number is fine

void clock(struct trap *tr)
{
    nclock++;
}

// prpintr() calls this.  No ПРП bit is ever up here, so it must never run.
void scintr(void)
{
    nclock--;
}

// extintr()'s drum arm.  mb.o is not linked -- the root disk is what this test reads --
// and no drum bit is ever armed, so like scintr() it must never run.
void mbintr(void)
{
    nclock--;
}

// The one thing that makes iowait() finish: delivery is blocked there, so the completion
// has to be polled rather than waited for.  See the header.
void sleep(chan_t chan, int pri)
{
    extintr();
}

void wakeup(chan_t chan)
{
}

// panic() must not return, and cannot report through main()'s accumulator, so it leaves a
// marker where the .ini can find it and spins.  The `step' count is what turns the spin
// into a failed run.
void panic(char *s)
{
    PANICWORD = PANICMARK;
    for (;;)
        ;
}

// sbcheck()'s only output channel, and md.c's.  The real ones are in kernel/prf.c, which
// would drag in the whole of printf(); counting the calls costs nothing and lets checks 6
// and 7 insist that sbcheck() reported what it rejected and kept quiet about what it did
// not.
static int nprdev;

void prdev(char *str, dev_t dev)
{
    nprdev++;
}

void deverror(struct buf *bp, int o1, int o2)
{
    nprdev++;
}

// wcopy()/wzero() live in kernel/besm6.S, which cannot go into a standalone test; alloc.o
// names them from alloc()/free()/update(), none of which run here.
void wcopy(const void *src, void *dst, int nwords)
{
    register const int *s = (const int *)src;
    register int *d       = (int *)dst;

    while (nwords-- > 0)
        *d++ = *s++;
}

void wzero(void *dst, int nwords)
{
    register int *p = (int *)dst;

    while (nwords-- > 0)
        *p++ = 0;
}

// alloc.o's ialloc()/update() name these; the inode layer is not linked and neither
// function runs here.  inode[] is conf.c's, and is storage for the same reason.
struct inode inode[NINODE];

struct inode *iget(dev_t dev, ino_t ino)
{
    return (NULL);
}

void iput(struct inode *ip)
{
}

void iupdat(struct inode *ip, time_t *ta, time_t *tm)
{
}

// -------------------------------------------------------------------------

static unsigned mask;

// kernel/main.c's binit(), less the nblkdev count and with the buffer store at a fixed
// physical page instead of the kernel's absolute `buffers'.  Keep it in step with that
// function: this is the same list, in the same order, and the free list it builds is what
// decides which buffer a bread() gets.
static void bufinit(void)
{
    register struct buf *bp;
    register int i;

    bfreelist.b_forw = bfreelist.b_back = bfreelist.av_forw = bfreelist.av_back = &bfreelist;
    for (i = 0; i < NBUF; i++) {
        bp                       = &buf[i];
        bp->b_dev                = NODEV;
        bp->b_addr          = (caddr_t)(int *)(BUFWORD + i * BSIZEW);
        bp->b_back               = &bfreelist;
        bp->b_forw               = bfreelist.b_forw;
        bfreelist.b_forw->b_back = bp;
        bfreelist.b_forw         = bp;
        bp->b_flags              = B_BUSY;
        brelse(bp);
    }
    mdtab.b_forw = mdtab.b_back = &mdtab;
}

int main(void)
{
    register struct buf *bp;
    register struct filsys *fp;
    struct buf *bp2;
    struct dinode *dp;

    intrinit();
    bufinit();

    // ---- Check 1: the superblock arrives at all -----------------------------------
    //
    // Block 1 of the root disk, through the buffer cache and the real driver.  Delivery
    // is open here -- crt0.s cleared БлПр and bread() calls mdstrategy() at spl0 -- so the
    // completion comes through the 0501 gate the way it does in the kernel; iowait()'s
    // sleep() stub is the backstop for the window in which it does not.
    //
    // Nothing below means anything if this failed, so it is the one check that returns
    // early rather than accumulating.
    bp = bread(rootdev, SUPERB);
    if (bp->b_flags & B_ERROR)
        return (F_RDERR);
    fp = (struct filsys *)bp->b_addr;

    // ---- Check 2: sbcheck(), the point of the exercise ------------------------------
    if (sbcheck(fp, rootdev) != 0)
        mask |= F_SBCHK;

    // ---- Check 3: the geometry the tool and the kernel have to agree on -------------
    //
    // sbcheck() tests these too; asserting them here as well is what distinguishes "the
    // image is wrong" from "sbcheck() is wrong", which is worth having in a test whose
    // whole purpose is to run sbcheck() for the first time.
    if (fp->s_magic != FS_MAGIC || fp->s_bsize != BSIZEW || fp->s_inopb != INOPB ||
        fp->s_naddr != NADDR)
        mask |= F_GEOM;
    if (fp->s_fsize != ROOTBLKS || fp->s_isize <= SUPERB || fp->s_isize >= fp->s_fsize)
        mask |= F_GEOM;

    // ---- Check 4: this is the image THIS BUILD wrote ---------------------------------
    if (fp->s_time != ROOTTIME)
        mask |= F_TIME;

    // ---- Check 5: the free lists are populated ---------------------------------------
    //
    // sbcheck() bounds these from above; a fresh mkfs must also have put something in
    // them, or the volume was built empty and every check above passed on a superblock
    // nothing could be allocated from.
    if (fp->s_nfree <= 0 || fp->s_nfree > NICFREE || fp->s_ninode <= 0 ||
        fp->s_ninode > NICINOD)
        mask |= F_FREE;

    // ---- Check 6: prdev() said nothing, and the cache holds the block ----------------
    //
    // The sentinel is what makes the second half a real check: a bread() that went back to
    // the device would hand back FS_MAGIC and be indistinguishable from a cache hit.
    if (nprdev != 0)
        mask |= F_NOISY;

    fp->s_magic = CACHEPAT;
    brelse(bp);

    if (!incore(rootdev, SUPERB))
        mask |= F_CACHE;
    bp2 = bread(rootdev, SUPERB);
    if (bp2 != bp || ((struct filsys *)bp2->b_addr)->s_magic != CACHEPAT)
        mask |= F_CACHE;
    brelse(bp2);

    // ---- Check 7: a block that is not a superblock ----------------------------------
    //
    // Block 0 is the boot block: b6fsutil leaves it zero.  Reading it exercises the driver
    // a second time, and sbcheck() must refuse it AND report it.  This is the check that
    // makes checks 2-5 mean something.
    bp = bread(rootdev, (daddr_t)0);
    if (bp->b_flags & B_ERROR)
        mask |= F_RDERR;
    if (sbcheck((struct filsys *)bp->b_addr, rootdev) == 0)
        mask |= F_BITE;
    if (nprdev != 1)
        mask |= F_QUIET;
    brelse(bp);

    // ---- Check 8: the root inode is the directory the manifest asked for -------------
    //
    // itod()/itoo() land ROOTINO in the first i-list block, read with nothing but bread()
    // and a struct cast -- the inode layer is not linked here.  nlink counts `.', `..' and
    // one entry per subdirectory, so a tree with any directories in it is > 2.
    bp = bread(rootdev, itod(ROOTINO));
    if (bp->b_flags & B_ERROR)
        mask |= F_RDERR;
    dp = (struct dinode *)bp->b_addr + itoo(ROOTINO);
    if ((dp->di_mode & S_IFMT) != S_IFDIR || dp->di_nlink < 2)
        mask |= F_ROOTI;
    brelse(bp);

    if (PANICWORD == PANICMARK)
        mask |= F_PANIC;

    return (mask);
}
