/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * Random set of variables
 * used by more than one
 * routine.
 */
extern char canonb[CANBSIZ];  /* buffer for erase and kill (#@) */
extern struct inode *rootdir; /* pointer to inode of root directory */
extern struct proc *runq;     /* head of linked list of running processes */
extern int cputype;           /* type of cpu =40, 45, or 70 */
extern int lbolt;             /* time of day in 60th not in time */
extern time_t time;           /* time in sec from 1970 */

/*
 * Nblkdev is the number of entries
 * (rows) in the block switch. It is
 * set in binit/bio.c by making
 * a pass over the switch.
 * Used in bounds checking on major
 * device numbers.
 */
extern int nblkdev;

/*
 * Number of character switch entries.
 * Set by cinit/tty.c
 */
extern int nchrdev;

extern int mpid;    /* generic for unique process id's */
extern char runin;  /* scheduling flag */
extern char runout; /* scheduling flag */
extern char runrun; /* scheduling flag */
extern char curpri; /* more scheduling */
extern int maxmem;  /* actual max memory per process */
extern int uhome;   /* whose u-area is live at UBASE (its p_addr) */
/*
 * ... or NOUHOME, meaning the live u-area belongs to no in-core image because the image it
 * belonged to has just been freed.  resume() must then load without flushing first, or it
 * would write 1024 words into core that malloc() may already have handed to someone else.
 * 0 is a safe sentinel: no process image ever lives at physical 0.  The rules for who
 * maintains this are written up once, at xswap() in kernel/text.c.
 */
#define NOUHOME 0
extern daddr_t swplo;        /* block number of swap space */
extern int nswap;            /* size of swap space */
extern int updlock;          /* lock for sync */
extern daddr_t rablock;      /* block to be read ahead */
extern char regloc[];        /* locs. of saved user registers (trap.c) */
extern char msgbuf[MSGBUFS]; /* saved "printf" characters */
extern dev_t rootdev;        /* device of the root */
extern dev_t swapdev;        /* swapping device */
extern dev_t pipedev;        /* pipe device */
extern int icode[];          /* user init code */
extern int szicode;          /* its size */

daddr_t bmap(struct inode *ip, daddr_t bn, int rwflg);
struct inode *ialloc(dev_t dev);
struct inode *iget(dev_t dev, ino_t ino);
void iput(struct inode *ip);
void iupdat(struct inode *ip, time_t *ta, time_t *tm);
void itrunc(struct inode *ip);
void wdir(struct inode *ip);
struct inode *owner(void);
struct inode *maknode(int mode);
struct inode *namei(int (*func)(void), int flag);
struct buf *alloc(dev_t dev);
struct buf *getblk(dev_t dev, daddr_t blkno);
struct buf *geteblk(void);
struct buf *bread(dev_t dev, daddr_t blkno);
struct buf *breada(dev_t dev, daddr_t blkno, daddr_t rablkno);
void bawrite(struct buf *bp);
void brelse(struct buf *bp);
struct filsys *getfs(dev_t dev);
int sbcheck(struct filsys *fp, dev_t dev); /* 0 = plausible superblock, 1 = reject */
struct file *getf(int f);
struct file *falloc(void);
int uchar(void);
int schar(void);
void plock(struct inode *ip);
void prele(struct inode *ip);
int min(int a, int b);
void psignal(struct proc *p, int sig);
void wakeup(chan_t chan);
void setrun(struct proc *p);
void swtch(void);
void exece(void);
void exit(int rv);
void nullsys(void);
void rexit(void);
void fork(void);
void read(void);
void write(void);
void open(void);
void close(void);
void wait(void);
void creat(void);
void link(void);
void unlink(void);
void exec(void);
void chdir(void);
void gtime(void);
void mknod(void);
void chmod(void);
void chown(void);
void sbreak(void);
void stat(void);
void seek(void);
void getpid(void);
void smount(void);
void sumount(void);
void setuid(void);
void getuid(void);
void stime(void);
void ptrace(void);
void alarm(void);
void fstat(void);
void pause(void);
void utime(void);
void stty(void);
void gtty(void);
void saccess(void);
void nice(void);
void ftime(void);
void sync(void);
void kill(void);
void nosys(void);
void dup(void);
void pipe(void);
void times(void);
void profil(void);
void setgid(void);
void getgid(void);
void ssig(void);
void sysacct(void);
void sysphys(void);
void syslock(void);
void ioctl(void);
void mpxchan(void);
void umask(void);
void chroot(void);
void nullopen(dev_t, int);
void nullclose(dev_t, int);
void nullrw(dev_t);
void nullioctl(dev_t, int, caddr_t, int);
int suser(void);
int compress(time_t t);
void writei(struct inode *ip);
void sleep(chan_t chan, int pri);
void prdev(char *str, dev_t dev);
void wcopy(const void *src, void *dst, int nwords);
void wzero(void *dst, int nwords);
void clrbuf(struct buf *bp);
void bwrite(struct buf *bp);
void panic(char *s);
void bflush(dev_t dev);
/*
 * Interrupt priority.  The BESM-6 has one interrupt level, not the PDP-11's eight, so this
 * kernel has exactly two: spl0 enables delivery, everything above it blocks (kernel/intr.c).
 * Only the two ends are real routines; the graded levels v7 callers write are aliases, so
 * `s = spl6(); ... splx(s);' still reads as it always did and still costs one instruction.
 */
int spl0(void), spl1(void);
#define spl4() spl1()
#define spl5() spl1()
#define spl6() spl1()
#define spl7() spl1()
void splx(int);
void mprpon(unsigned bits);  /* unmask a device's ПРП interrupts (intr.c) */
void mgrpon(unsigned bits);  /* arm a device's ГРП bits for one exchange (intr.c) */
void mgrpoff(unsigned bits); /* ... and disarm them again; see the pair in intr.c */
void addupc(int, void *, int);
int setpri(struct proc *pp);
void xrele(struct inode *ip);
void printf(char *fmt, ...);
void ifree(dev_t dev, ino_t ino);
void free(dev_t dev, daddr_t bno);
void bdwrite(struct buf *bp);
int grow(int pg); /* pg is a virtual PAGE number, not an address */
int subyte(caddr_t addr, int value);
int suword(caddr_t addr, int value);
int fubyte(caddr_t addr);
int fuword(caddr_t addr);
void startup(void);
void clkstart(void);
void cinit(void);
int newproc(void);
void expand(int newsize);
int estabur(int nt, int nd, int ns, int sep, int xrw);
int copyout(caddr_t from, caddr_t to, int nbytes);
int copyin(caddr_t from, caddr_t to, int nbytes);
void sched(void);
int access(struct inode *ip, int mode);
void readi(struct inode *ip);
void putchar(int c);
void update(void);
void idle(void);
int cpass(void);
int passc(int c);
int fsig(struct proc *p);
int procxmt(void);
void sendsig(caddr_t p, int signo);
int core(void);
void copyseg(int s, int d);
void clearseg(int d);
int issig(void);
int save(label_t);
void resume(int, label_t);  /* a physical word address: 19 bits, not a `short' */
void intrinit(void);        /* arm the always-live ГРП sources; the level is БлПр (intr.c) */
extern volatile int idling; /* set while the idle spin runs; clock() charges idle time */
int swapin(struct proc *p);
void xswap(struct proc *p, int ff, int os);
void swap(int blkno, int coreaddr, int count, int rdflg);
void sureg(void);
/*
 * The u-area bracket (kernel/uarea.s).  The live u-area is at UBASE; a process's home copy is
 * the first page of its image at p_addr, above 0100000 and out of reach of an unmapped access.
 * uflush() only reads the live page and may be called from C; uload() overwrites it -- and with
 * it the kernel stack its caller is standing on -- so only resume() may call it.  See
 * kernel/TODO.md, "The u-area invariant".
 */
void uflush(paddr_t paddr);
void uload(paddr_t paddr);
int getxfile(struct inode *ip, int nargc);
void xalloc(struct inode *ip);
void xfree(void);
void closef(struct file *fp);
void acct(void);
void readp(struct file *fp);
void writep(struct file *fp);
void openi(struct inode *ip, int rw);
int ufalloc(void);
dev_t getmdev(void);
void xumount(int dev);
void qswtch(void);
void psig(void);
paddr_t physaddr(int addr);
int useracc(int addr, int count, int rw);
int physrange(int addr, int count);
void timeout(void (*fun)(carg_t), carg_t arg, int tim);
void deverror(struct buf *bp, int o1, int o2);
void iodone(struct buf *bp);
void physio(void (*strat)(struct buf *), struct buf *bp, int dev, int rw);
void open1(struct inode *ip, int mode, int trf);
void signal(int pgrp, int sig);
void iomove(caddr_t cp, int n, int flag);

/*
 * Instrumentation
 */
extern int dk_busy;
extern int dk_time[32];
extern int dk_numb[3];
extern int dk_wds[3];
extern int tk_nin;
extern int tk_nout;

/*
 * Structure of the system-entry table.
 *
 * NSYSENT must match the array bound in kernel/sysent.c: syscall() RANGE-CHECKS
 * the number the user put in the `$77 N' effective address against it rather
 * than masking, so a table and a check that drift apart would dispatch garbage.
 *
 * sy_nrarg is vestigial on this machine and is read nowhere.  It counted the
 * PDP-11's args-already-in-registers; here the count is fixed by the ABI --
 * exactly one argument (the last) arrives in the accumulator for any narg >= 1,
 * and the rest are on the user stack (doc/Besm6_Calling_Conventions.md).
 */
#define NSYSENT 64

extern struct sysent {
    char sy_narg;          /* total number of arguments */
    char sy_nrarg;         /* number of args in registers (unused: see above) */
    void (*sy_call)(void); /* handler */
} sysent[];
