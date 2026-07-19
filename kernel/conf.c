// clang-format off
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
 * XXX minor 56 is an x86 partition number that nothing on this machine interprets --
 * mdstrategy() ignores b_dev entirely.  Task 18b.4 derives the real unit/partition map.
 */
dev_t rootdev = makedev(0, 56);
dev_t swapdev = makedev(1, 0); /* the drums are the paging store */
dev_t pipedev = makedev(0, 56);
int nldisp    = 1;
daddr_t swplo = 0;
int nswap     = 32000; /* XXX fiction: two drums hold 1024 blocks.  Task 18b.6 sizes it. */

struct buf buf[NBUF];
struct file file[NFILE];
struct inode inode[NINODE];
struct proc proc[NPROC];
struct text text[NTEXT];
struct buf bfreelist;
struct acct acctbuf;
struct inode *acctp;
