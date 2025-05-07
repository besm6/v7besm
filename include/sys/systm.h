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

dev_t getmdev();
daddr_t bmap();
struct inode *ialloc(dev_t dev);
struct inode *iget(dev_t dev, ino_t ino);
void iput(struct inode *ip);
void iupdat(struct inode *ip, time_t *ta, time_t *tm);
void itrunc(struct inode *ip);
void wdir(struct inode *ip);
struct inode *owner();
struct inode *maknode();
struct inode *namei();
struct buf *alloc(dev_t dev);
struct buf *getblk(dev_t dev, daddr_t blkno);
struct buf *geteblk();
struct buf *bread(dev_t dev, daddr_t blkno);
struct buf *breada(dev_t dev, daddr_t blkno, daddr_t rablkno);
void bawrite(struct buf *bp);
void brelse(struct buf *bp);
struct filsys *getfs(dev_t dev);
struct file *getf();
struct file *falloc();
int uchar();
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
