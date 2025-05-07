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

int nulldev();
int nodev();
int hdopen(), hdstrategy();
extern struct buf hdtab;
int fdstrategy();
extern struct buf fdtab;
int mdstrategy();
extern struct buf mdtab;

struct bdevsw bdevsw[] = {
#if 1
    { hdopen, nulldev, hdstrategy, &hdtab },  /* hd = 0 */
    { nulldev, nulldev, fdstrategy, &fdtab }, /* fd = 1 */
    { nulldev, nulldev, mdstrategy, &mdtab }, /* md = 2 */
#endif
    {},
};

int scopen(), scclose(), scread(), scwrite(), scioctl();
int mmread(), mmwrite();
int syopen(), syread(), sywrite(), sysioctl();
int sropen(), srclose(), srread(), srwrite(), srioctl();
extern struct tty sr[];
int hdread(), hdwrite();
int fdread(), fdwrite();
int mdread(), mdwrite();
int cdread();

struct cdevsw cdevsw[] = {
#if 1
    { scopen, scclose, scread, scwrite, scioctl, nulldev, 0 },  /* console = 0 */
    { nulldev, nulldev, mmread, mmwrite, nodev, nulldev, 0 },   /* mem = 1 */
    { syopen, nulldev, syread, sywrite, sysioctl, nulldev, 0 }, /* tty = 2 */
    { sropen, srclose, srread, srwrite, srioctl, nulldev, sr }, /* sr = 3 */
    { hdopen, nulldev, hdread, hdwrite, nodev, nulldev, 0 },    /* hd = 4 */
    { nulldev, nulldev, fdread, fdwrite, nodev, nulldev, 0 },   /* fd = 5 */
    { nulldev, nulldev, mdread, mdwrite, nodev, nulldev, 0 },   /* md = 6 */
    { nulldev, nulldev, cdread, nodev, nodev, nulldev, 0 },     /* cd = 7 */
#endif
    {},
};

int ttyopen(), ttyclose(), ttread(), ttwrite(), ttyinput(), ttstart();

struct linesw linesw[] = {
    { ttyopen, nulldev, ttread, ttwrite, nodev, ttyinput, ttstart }, /* 0 */
    {},
};

dev_t rootdev = makedev(0, 56);
dev_t swapdev = makedev(0, 57);
dev_t pipedev = makedev(0, 56);
int nldisp    = 1;
daddr_t swplo = 0;
int nswap     = 32000;

struct buf buf[NBUF];
struct file file[NFILE];
struct inode inode[NINODE];
int mpxchan();
int (*ldmpx)() = mpxchan;
struct proc proc[NPROC];
struct text text[NTEXT];
struct buf bfreelist;
struct acct acctbuf;
struct inode *acctp;
