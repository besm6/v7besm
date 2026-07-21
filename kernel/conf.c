// clang-format off
#include "sys/types.h"
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/buf.h"
#include "sys/tty.h"
#include "sys/conf.h"
#include "sys/proc.h"
#include "sys/text.h"
#include "sys/dir.h"
#include "sys/user.h"
#include "sys/file.h"
#include "sys/inode.h"
#include "sys/acct.h"
// clang-format on

void mdopen(dev_t, int);
void mdstrategy(struct buf *);
extern struct buf mdtab;
void mbopen(dev_t, int);
void mbstrategy(struct buf *);
extern struct buf mbtab;

/*
 * Every row needs a non-null d_open: main() counts nblkdev by walking this table until
 * one is null, so a hole would truncate it rather than skip a device.
 */
struct bdevsw bdevsw[] = {
    { mdopen, nullclose, mdstrategy, &mdtab }, /* md = 0, magnetic disk */
    { mbopen, nullclose, mbstrategy, &mbtab }, /* mb = 1, magnetic drum */
    {},
};

void scopen(dev_t, int), scclose(dev_t, int);
void scread(dev_t), scwrite(dev_t);
void scioctl(dev_t, int, caddr_t, int);
void mmread(dev_t), mmwrite(dev_t);
void syopen(dev_t, int);
void syread(dev_t), sywrite(dev_t);
void sysioctl(dev_t, int, caddr_t, int);
void sropen(dev_t, int), srclose(dev_t, int);
void srread(dev_t), srwrite(dev_t);
void srioctl(dev_t, int, caddr_t, int);
extern struct tty sr[];
void mdread(dev_t), mdwrite(dev_t);
void mbread(dev_t), mbwrite(dev_t);

struct cdevsw cdevsw[] = {
    { scopen, scclose, scread, scwrite, scioctl, nulldstop, 0 },       /* console = 0 */
    { nullopen, nullclose, mmread, mmwrite, nullioctl, nulldstop, 0 }, /* mem = 1 */
    { syopen, nullclose, syread, sywrite, sysioctl, nulldstop, 0 },    /* tty = 2 */
    { sropen, srclose, srread, srwrite, srioctl, nulldstop, sr },      /* sr = 3 */
    { mdopen, nullclose, mdread, mdwrite, nullioctl, nulldstop, 0 },   /* md = 4 */
    { mbopen, nullclose, mbread, mbwrite, nullioctl, nulldstop, 0 },   /* mb = 5 */
    {},
};

struct linesw linesw[] = {
    { ttyopen, nulltclose, ttread, ttwrite, nulltioctl, ttyinput, ttstart }, /* 0 */
    {},
};

/*
 * The disk minor number is a flat drive index -- bit 5 the controller, bits 4-3 the group
 * (линейка), bits 2-0 the drive -- chosen to be identical to the simulator's own unit
 * subscript, so that a minor number and a SIMH unit name are the same number.  There are no
 * partitions: one drive is 2000 blocks, about 6 Mb, and swap is on the drums.  So minor 0 is
 * controller 3, group 0, drive 0 = SIMH's MD0 unit 0.  dev/md.c has the layout in full.
 */
dev_t rootdev = makedev(0, 0);
dev_t swapdev = makedev(1, 0); /* the drums are the paging store */
dev_t pipedev = makedev(0, 0);
int nldisp    = 1;

/*
 * The whole of both drums is swap space: 2 drums * 256 zones * 2 blocks = 1024 blocks,
 * and dev/mb.c makes the two of them one linear block space so that this is a single
 * number rather than a per-unit map.
 *
 * 1024, not 1023: machdep.c frees blocks 1..nswap into swapmap and then decrements swplo,
 * so swap block 1 is device block 0 and swap block nswap is device block 1023.
 */
daddr_t swplo = 0;
int nswap     = 1024;

struct buf buf[NBUF];
struct file file[NFILE];
struct inode inode[NINODE];
struct proc proc[NPROC];
struct text text[NTEXT];
struct buf bfreelist;
struct acct acctbuf;
struct inode *acctp;
