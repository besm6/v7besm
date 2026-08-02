// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// sys/param.h and sys/types.h are INCLUDED, not assumed of the caller -- sys/dir.h's
// precedent, and for its reason: this file cannot compile without either, the externs below
// naming CANBSIZ and MSGBUFS and the prototypes naming nine of the scalar typedefs.
// <besm6.h> is here for splx(): that is a macro over __besm6_setpsw(), so every caller of it
// needs the intrinsic declared, and six of them have no other reason to name <besm6.h>.

#ifndef _SYS_SYSTM_H
#define _SYS_SYSTM_H

#include <besm6.h>
#include <sys/param.h>
#include <sys/types.h>

// The five structures this header only ever names THROUGH A POINTER.  These declarations
// are load-bearing, not decoration: a struct tag first named inside a function prototype's
// parameter list is declared in PROTOTYPE SCOPE -- a type distinct from the one sys/inode.h
// goes on to define at file scope -- so without them every `struct inode *' below would be a
// different type from every caller's, and b6cc does not say so.  Pointers are all that is
// wanted, so the five headers themselves stay out: 48 of the 51 kernel translation units
// include this one, and most want none of the ~60 macros those carry.  sys/conf.h and
// sys/tty.h declare theirs the same way, their function-pointer members having parameter
// lists too.
struct buf;
struct file;
struct filsys;
struct inode;
struct proc;

// Random set of variables
// used by more than one
// routine.
extern char canonb[CANBSIZ];  // buffer for erase and kill (#@)
extern struct inode *rootdir; // pointer to inode of root directory
extern struct proc *runq;     // head of linked list of running processes
extern int cputype;           // type of cpu =40, 45, or 70
extern int lbolt;             // time of day in 60th not in time
extern time_t time;           // time in sec from 1970

// Nblkdev is the number of entries
// (rows) in the block switch. It is
// set in binit/bio.c by making
// a pass over the switch.
// Used in bounds checking on major
// device numbers.
extern int nblkdev;

// Number of character switch entries.
// Set by cinit/tty.c
extern int nchrdev;

extern int mpid;    // generic for unique process id's
extern char runin;  // scheduling flag
extern char runout; // scheduling flag
extern char runrun; // scheduling flag
extern char curpri; // more scheduling
extern int maxmem;  // actual max memory per process
extern int phymem;  // words of physical core (machdep.c); the length of /dev/mem
extern int uhome;   // whose u-area is live at UBASE (its p_addr)
// ... or NOUHOME, meaning the live u-area belongs to no in-core image because the image it
// belonged to has just been freed.  resume() must then load without flushing first, or it
// would write a dead u-area into core that malloc() may already have handed to someone else.
// 0 is a safe sentinel: no process image ever lives at physical 0.  The rules for who
// maintains this are written up once, at xswap() in kernel/text.c.
#define NOUHOME 0

// Swapper and shared-text traffic counters.  They exist to be OBSERVED: a load test that
// only checks the machine survived cannot tell a kernel that swapped from one that never
// filled core, and a shared text that was never joined looks exactly like one that was.
// kernel/test/swap asserts each of these is non-zero, which is what stops it from passing
// for the wrong reason -- the same argument mdretries in kernel/dev/md.c was added for.
// Plain ints: every site runs in the swapper or at spl6, and a lost count is not a bug
// worth an spl bracket.
extern int nswapout;  // xswap():  images written to the paging store (kernel/text.c)
extern int nswapin;   // swapin(): images read back (kernel/slp.c)
extern int ntextin;   // text segments read off the paging store
extern int ntextout;  // ... and written to it, by the last in-core sharer leaving
extern int ntextjoin; // xalloc(): a process attached to a text already in text[]

// iomove() traffic, in BYTES, by the arm that carried it (kernel/ucopy.c).  Same argument as
// the counters above: a copy path that is never taken looks exactly like one that is, and the
// split between the two byte arms is what says how much is left to win.
extern int niobulk;  // whole words through copyin/copyout, both pointers on byte #0
extern int nioedge;  // byte-at-a-time, squaring up the two ends of an in-phase transfer
extern int nioshift; // byte-at-a-time, phases DIFFERENT -- needs a shifting copy, or nothing

extern daddr_t swplo;        // block number of swap space
extern int nswap;            // size of swap space
extern int updlock;          // lock for sync
extern daddr_t rablock;      // block to be read ahead
extern char regloc[];        // locs. of saved user registers (trap.c)
extern char msgbuf[MSGBUFS]; // saved "printf" characters
extern char *msgbufp;        // ... and where putchar() will put the next one (dev/sc.c).
                             // Declared beside the buffer because the two are useless apart:
                             // dmesg reads both through kctl(2) and needs the pointer to know
                             // where the ring's oldest character is.
extern dev_t rootdev;        // device of the root
extern dev_t swapdev;        // swapping device
extern dev_t pipedev;        // pipe device
// The user bootstrap (kernel/besm6.S), and the word past its end: main() sizes the copy as
// `eicode - icode' rather than from a szicode of its own, the way machdep.c sizes bss from
// `end - edata'.  Both are array declarations so that the NAME is the address.
extern int icode[];  // user init code
extern int eicode[]; // ... and the first word past it

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
int sbcheck(struct filsys *fp, dev_t dev); // 0 = plausible superblock, 1 = reject
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
void sigret(void); // the return half of the signal frame; kernel/sendsig.c
void setgid(void);
void getgid(void);
void ssig(void);
void kctl(void); // the kernel-variable table; kernel/ksym.c, <sys/kctl.h>
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
// Interrupt priority.  The BESM-6 has one interrupt level, not the PDP-11's eight, so this
// kernel has exactly two: spl0 enables delivery, everything above it blocks (kernel/intr.c).
// Only the two ends are real routines; the graded levels v7 callers write are aliases, so
// `s = spl6(); ... splx(s);' still reads as it always did and still costs one instruction.
void spl0(void); // the base level is never saved and restored -- nothing is below it
int spl1(void);
#define spl4() spl1()
#define spl5() spl1()
#define spl6() spl1()
#define spl7() spl1()
// ... and splx() is the whole of one instruction, so it is a macro too.  `s' is the mode word
// spl1() handed out, never a level: splx(0) would clear БлП and БлЗ.  See kernel/intr.c.
#define splx(s) __besm6_setpsw(s)
void mprpon(unsigned bits);  // unmask a device's ПРП interrupts (intr.c)
void mgrpon(unsigned bits);  // arm a device's ГРП bits for one exchange (intr.c)
void mgrpoff(unsigned bits); // ... and disarm them again; see the pair in intr.c
void addupc(int, void *, int);
int setpri(struct proc *pp);
void xrele(struct inode *ip);
void printf(char *fmt, ...);
void ifree(dev_t dev, ino_t ino);
void free(dev_t dev, daddr_t bno);
void bdwrite(struct buf *bp);
int grow(int pg); // pg is a virtual PAGE number, not an address
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
// ... and the byte-granular pair over them (kernel/ucopy.c), which is what iomove() calls:
// these honour the fat pointers' byte offsets, where copyin/copyout mask them away.
int copyoutb(caddr_t from, caddr_t to, int nbytes);
int copyinb(caddr_t from, caddr_t to, int nbytes);
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
// n words between two PHYSICAL word addresses, through the same mapped window copyseg uses
// (kernel/seg.S).  Neither run may cross a page boundary, and neither may lie in physical
// page 0 -- dev/mem.c, its only caller, chops against both.
void copyphys(paddr_t s, paddr_t d, int n);
int issig(void);
int save(label_t);
void resume(int, label_t);  // a physical word address: 19 bits, not a `short'
void intrinit(void);        // arm the always-live ГРП sources; the level is БлПр (intr.c)
extern volatile int idling; // set while the idle spin runs; clock() charges idle time
int swapin(struct proc *p);
void xswap(struct proc *p, int ff, int os);
void swap(int blkno, int coreaddr, int count, int rdflg);
void sureg(void);
// The u-area bracket (kernel/uarea.s).  The live u-area is at UBASE; a process's home copy is
// the first page of its image at p_addr, above 0100000 and out of reach of an unmapped access.
// At most USIZE words -- the saved page only, never the overflow page above it (see UBASE in
// param.h) -- and in practice far fewer: uflush() copies as far as r15 has reached and leaves
// the count in u_stkdepth for uload() to read back (task 30).  uflush() only reads the live
// page and may be called from C, but ONLY from a frame at least as deep as the labels armed in
// it; uload() overwrites the live page -- and with it the kernel stack its caller is standing
// on -- so only resume() may call it.  See kernel/README.md, "The u-area invariant".
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

// Instrumentation
extern int dk_busy;
extern int dk_time[32];
extern int dk_numb[3];
extern int dk_wds[3];
extern int tk_nin;
extern int tk_nout;

// Structure of the system-entry table.
//
// NSYSENT must match the array bound in kernel/sysent.c: syscall() RANGE-CHECKS
// the number the user put in the `$77 N' effective address against it rather
// than masking, so a table and a check that drift apart would dispatch garbage.
//
// sy_nrarg is vestigial on this machine and is read nowhere.  It counted the
// PDP-11's args-already-in-registers; here the count is fixed by the ABI --
// exactly one argument (the last) arrives in the accumulator for any narg >= 1,
// and the rest are on the user stack (doc/Besm6_Calling_Conventions.md).
#define NSYSENT 64

extern struct sysent {
    char sy_narg;          // total number of arguments
    char sy_nrarg;         // number of args in registers (unused: see above)
    void (*sy_call)(void); // handler
} sysent[];

#endif // _SYS_SYSTM_H
