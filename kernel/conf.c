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

void hdopen(dev_t, int);
int hdstrategy();
extern struct buf hdtab;
int fdstrategy();
extern struct buf fdtab;
int mdstrategy();
extern struct buf mdtab;

struct bdevsw bdevsw[] = {
    { hdopen, nullclose, hdstrategy, &hdtab },   /* hd = 0 */
    { nullopen, nullclose, fdstrategy, &fdtab }, /* fd = 1 */
    { nullopen, nullclose, mdstrategy, &mdtab }, /* md = 2 */
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
void hdread(dev_t), hdwrite(dev_t);
void fdread(dev_t), fdwrite(dev_t);
void mdread(dev_t), mdwrite(dev_t);
void cdread(dev_t);

struct cdevsw cdevsw[] = {
    { scopen, scclose, scread, scwrite, scioctl, nulldstop, 0 },       /* console = 0 */
    { nullopen, nullclose, mmread, mmwrite, nullioctl, nulldstop, 0 }, /* mem = 1 */
    { syopen, nullclose, syread, sywrite, sysioctl, nulldstop, 0 },    /* tty = 2 */
    { sropen, srclose, srread, srwrite, srioctl, nulldstop, sr },      /* sr = 3 */
    { hdopen, nullclose, hdread, hdwrite, nullioctl, nulldstop, 0 },   /* hd = 4 */
    { nullopen, nullclose, fdread, fdwrite, nullioctl, nulldstop, 0 }, /* fd = 5 */
    { nullopen, nullclose, mdread, mdwrite, nullioctl, nulldstop, 0 }, /* md = 6 */
    { nullopen, nullclose, cdread, nullrw, nullioctl, nulldstop, 0 },  /* cd = 7 */
    {},
};

struct linesw linesw[] = {
    { ttyopen, nulltclose, ttread, ttwrite, nulltioctl, ttyinput, ttstart }, /* 0 */
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
struct proc proc[NPROC];
struct text text[NTEXT];
struct buf bfreelist;
struct acct acctbuf;
struct inode *acctp;
