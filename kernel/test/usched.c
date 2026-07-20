/*
 * usched -- newproc() and swtch() alternate between two processes, on the real machine.
 *
 * The "done when" of task 16, and the sixth standalone SIMH test.  Where uswtch drives
 * save()/resume() directly, this one links the REAL scheduler -- kernel/slp.o -- and lets it
 * do the switching: newproc() forks proc[1] out of proc[0]'s image, and the two then hand the
 * machine back and forth through v7's own save/resume dance.
 *
 * What it proves that uswtch cannot: that the invariant survives the code that actually
 * exercises it.  newproc() must uflush() before copying the parent's image, or the child
 * inherits a stale u_ssav and never returns from its save(); swtch()'s three resume() sites
 * must each land in the right one of three different labels; and each process must see its
 * own u-area, which is checked with a sentinel written into `u' itself.
 *
 * The environment is hand-built and deliberately thin.  There is no disk on this machine yet
 * (task 18b), so swap() and xswap() are stubs that must never run: the coremap is seeded with
 * enough free core that malloc() always succeeds and newproc() takes its copyseg() branch.
 * If a stub fires, the mask says so rather than the test quietly proving less than it claims.
 *
 * The counters live in ordinary bss, NOT in the u-area, for the reason uswtch's phase does:
 * the u-area is swapped underneath them.
 */

// clang-format off
#include "sys/types.h"
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/map.h"
#include "sys/dir.h"
#include "sys/user.h"
#include "sys/proc.h"
#include "sys/inode.h"
#include "sys/text.h"
// clang-format on

void halt(int status);
struct trap;

/* map.h hides these behind #ifdef KERNEL, and a test compiles without it. */
int malloc(struct map *mp, int size);
void mfree(struct map *mp, int size, int a);

/* intr.c hands this to clock(); no frame is built here. */
int *intrframe;

/*
 * Physical layout.  proc[0]'s image is one page at 0100000 -- USIZE words, the u-area and
 * nothing else, exactly as main() sets it up in the real kernel -- and everything above that
 * is free core for newproc() to build the child in.
 */
#define P0     (32 * PGSZ) /* 0100000 */
#define COREBASE (33 * PGSZ)
#define CORESIZE (16 * PGSZ)

#define SENT_P 0525252 /* "the live u-area is the parent's" */
#define SENT_C 0252525 /* "... the child's" */

#define sentinel u.u_arg[0]

/* Failure bits, mirrored in usched.ini's legend. */
#define F_NOCHILD 00001 /* the child never ran: newproc()'s save() did not fire */
#define F_NOPAR   00002 /* the parent did not come back from swtch() */
#define F_CSENT   00004 /* the child saw the wrong u-area */
#define F_PSENT   00010 /* the parent did not get its own u-area back */
#define F_UHOME   00020 /* uhome did not follow the switches */
#define F_STUB    00040 /* a stub that must never run, ran (swap/xswap/panic) */
#define F_PROCP   00100 /* u.u_procp was not what the running process should see */
#define F_NEWPROC 00200 /* newproc() failed outright */

static int mask;
static int nchild;  /* times the child leg ran */
static int nparent; /* times the parent leg resumed */
static int childpa; /* the child's p_addr, noted by the parent */

/*
 * The kernel objects slp.o names.  proc[] and the maps are real; the inode is a single fake
 * one, because newproc() bumps u.u_cdir->i_count unconditionally.
 */
struct proc proc[NPROC];
struct map coremap[CMAPSIZ];
struct map swapmap[SMAPSIZ];
struct text text[NTEXT];
static struct inode fakecdir;

int maxmem;
int mpid;
char runrun, runin, runout;
char curpri;
struct proc *runq;
time_t time;
int lbolt;

/* --- stubs ------------------------------------------------------------------------- */

void panic(char *s)
{
    (void)s;
    mask |= F_STUB;
    halt(mask); /* do not spin in idle(): a panic here means the test is void */
}

void swap(int blkno, int coreaddr, int count, int rdflg)
{
    (void)blkno;
    (void)coreaddr;
    (void)count;
    (void)rdflg;
    mask |= F_STUB; /* there is no disk yet; the coremap is sized so this never runs */
}

void xswap(struct proc *p, int ff, int os)
{
    (void)p;
    (void)ff;
    (void)os;
    mask |= F_STUB;
}

void clock(struct trap *tr)
{
    (void)tr;
}

void scintr(void)
{
}

void mbintr(void)
{
}

void mdintr(void)
{
}

/* text.o is not linked (it would drag in the whole inode layer); nothing here has text. */
void xlock(struct text *xp)
{
    (void)xp;
    mask |= F_STUB;
}

void xunlock(struct text *xp)
{
    (void)xp;
    mask |= F_STUB;
}

void psignal(struct proc *p, int sig)
{
    (void)p;
    (void)sig;
}

int issig(void)
{
    return 0;
}

void printf(char *fmt, ...)
{
    (void)fmt;
}

/* --- the test ---------------------------------------------------------------------- */

int main()
{
    int n;

    /*
     * proc[0], exactly as kernel/main.c sets it up: its image is the u-area page, and the
     * live u-area at UBASE is its own.
     */
    maxmem         = CORESIZE;
    proc[0].p_addr = P0;
    uhome          = proc[0].p_addr;
    proc[0].p_size = USIZE;
    proc[0].p_stat = SRUN;
    proc[0].p_flag = SLOAD | SSYS;
    proc[0].p_nice = NZERO;
    proc[0].p_pid  = 0;
    u.u_procp      = &proc[0];
    u.u_cdir       = &fakecdir;
    u.u_rdir       = NULL;

    /* Free core for the child's image, above proc[0]'s page. */
    mfree(coremap, CORESIZE, COREBASE);

    sentinel = SENT_P; /* the live u-area is the parent's */

    n = newproc();
    if (n < 0) {
        mask |= F_NEWPROC;
        halt(mask);
    }

    if (n == 0) {
        /*
         * The parent, still proc[0].  newproc() left the child SRUN and on the run queue, so
         * swtch() will pick it: proc[0] takes the "resume the idle process" path, the loop
         * finds proc[1], and resume() lands in the child's newproc() save().
         */
        childpa = proc[1].p_addr;
        swtch();

        /*
         * Back again, resumed by the child below.  The u-area must be the parent's once more,
         * and uhome must have followed us home.
         */
        nparent++;
        if (sentinel != SENT_P)
            mask |= F_PSENT;
        if (uhome != P0)
            mask |= F_UHOME;
        if (u.u_procp != &proc[0])
            mask |= F_PROCP;
        if (nchild == 0)
            mask |= F_NOCHILD;
        halt(mask);
    }

    /*
     * The child, proc[1], reached through resume() into newproc()'s save().  It inherited the
     * parent's u-area -- sentinel and all -- which is exactly what "the child is a copy" means;
     * then it stamps the live copy as its own, so that the parent noticing SENT_P again on the
     * way back proves the u-area really was switched and not merely left alone.
     */
    nchild++;
    if (sentinel != SENT_P)
        mask |= F_CSENT;
    if (uhome != childpa)
        mask |= F_UHOME;
    if (u.u_procp != &proc[1])
        mask |= F_PROCP;
    sentinel = SENT_C;

    /* Hand the machine back.  proc[0] is not on the run queue, so put it there first. */
    setrun(&proc[0]);
    swtch();

    /* The child is never resumed again; reaching here means the alternation went wrong. */
    mask |= F_NOPAR;
    halt(mask);
    return 0;
}
