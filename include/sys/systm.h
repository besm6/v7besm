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

extern int mpid;             /* generic for unique process id's */
extern char runin;           /* scheduling flag */
extern char runout;          /* scheduling flag */
extern char runrun;          /* scheduling flag */
extern char curpri;          /* more scheduling */
extern int maxmem;           /* actual max memory per process */
extern physadr lks;          /* pointer to clock device */
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

struct chan;

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
struct file *getf(int f);
struct file *falloc(void);
int uchar(void);
void plock(struct inode *ip);
void prele(struct inode *ip);
unsigned min(unsigned a, unsigned b);
void psignal(struct proc *p, int sig);
void wakeup(caddr_t chan);
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
void nullclose(dev_t, int, struct chan *);
void nullrw(dev_t);
void nullioctl(dev_t, int, caddr_t, int);
int suser(void);
int compress(time_t t);
void writei(struct inode *ip);
void sleep(caddr_t chan, int pri);
void prdev(char *str, dev_t dev);
void bcopy(const void *src, void *dst, unsigned len);
void bzero(void *dst, unsigned len);
void clrbuf(struct buf *bp);
void bwrite(struct buf *bp);
void panic(char *s);
void bflush(dev_t dev);
int spl0(void), spl1(void), spl4(void), spl5(void), spl6(void), spl7(void);
void splx(int);
void addupc(int, void *, int);
int setpri(struct proc *pp);
void xrele(struct inode *ip);
void printf(char *fmt, ...);
void ifree(dev_t dev, ino_t ino);
void free(dev_t dev, daddr_t bno);
void bdwrite(struct buf *bp);
void outb(int addr, int value);
int inb(int addr);
void outsw(int, char *, int);
void insw(int, char *, int);
void cli(void);
void sti(void);
int grow(unsigned sp);
int subyte(caddr_t addr, int value);
int suword(caddr_t addr, int value);
int fubyte(caddr_t addr);
int fuword(caddr_t addr);
int inrtc(int addr);
int getrtc(int addr);
void startup(void);
void clkstart(void);
void cinit(void);
int newproc(void);
void expand(int newsize);
int estabur(int nt, int nd, int ns, int sep, int xrw);
int copyout(caddr_t from, caddr_t to, unsigned nbytes);
int copyin(caddr_t from, caddr_t to, unsigned nbytes);
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
void savfp(void *ptr);
void restfp(void *ptr);
void sendsig(caddr_t p, int signo);
int core(void);
void copyseg(int s, int d);
void clearseg(int d);
int issig(void);
int save(label_t);
void resume(short, label_t);
int swapin(struct proc *p);
void xswap(struct proc *p, int ff, int os);
void swap(int blkno, int coreaddr, int count, int rdflg);
void sureg(void);
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
int ld_cr0(void), ld_cr2(void), ld_cr3(void);
void stst(int*);
void psig(void);
void invd(void);
unsigned physaddr(unsigned addr);
void timeout(void (*fun)(caddr_t), caddr_t arg, int tim);
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
extern long dk_time[32];
extern long dk_numb[3];
extern long dk_wds[3];
extern long tk_nin;
extern long tk_nout;

/*
 * Structure of the system-entry table
 */
extern struct sysent {
    char sy_narg;          /* total number of arguments */
    char sy_nrarg;         /* number of args in registers */
    void (*sy_call)(void); /* handler */
} sysent[];
